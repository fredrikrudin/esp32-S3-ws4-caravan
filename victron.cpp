/* Victron BLE Instant Readout.
   Needs "Instant readout via Bluetooth" enabled in VictronConnect,
   and the device's encryption key (Product info -> Encryption data). */
#include "app.h"
#include "mbedtls/aes.h"

/* Reads little-endian bit fields (Victron payload format) */
struct BitReader {
  const uint8_t *d;
  size_t len;
  size_t pos;
  uint32_t u(int bits) {
    uint32_t v = 0;
    for (int i = 0; i < bits; i++, pos++) {
      size_t byte = pos >> 3;
      if (byte < len && ((d[byte] >> (pos & 7)) & 1)) v |= (1UL << i);
    }
    return v;
  }
  int32_t s(int bits) {
    uint32_t v = u(bits);
    if (v & (1UL << (bits - 1))) v |= ~((1UL << bits) - 1);
    return (int32_t)v;
  }
};

const char *vic_type_name(uint8_t t) {
  switch (t) {
    case 0x01: return "Solar charger";
    case 0x02: return "Battery monitor";
    case 0x03: return "Inverter";
    case 0x04: return "DC-DC";
    case 0x05: return "Smart Lithium";
    case 0x06: return "Inverter RS";
    case 0x08: return "AC charger";
    case 0x09: return "Battery protect";
    case 0x0A: return "Lynx BMS";
    case 0x0C: return "VE.Bus";
    case 0x0D: return "DC energy meter";
    default: return "Victron device";
  }
}

bool vic_supported(uint8_t t) {
  return t == VIC_SOLAR || t == VIC_BATTMON || t == VIC_DCDC || t == VIC_ACCHG || t == VIC_INVERTER;
}

const char *vic_state_name(uint8_t s) {
  switch (s) {
    case 0: return "Off";
    case 1: return "Low power";
    case 2: return "Fault";
    case 3: return "Bulk";
    case 4: return "Absorption";
    case 5: return "Float";
    case 6: return "Storage";
    case 7: return "Equalize";
    case 9: return "Inverting";
    case 11: return "Power supply";
    case 245: return "Starting up";
    case 246: return "Rep. absorption";
    case 247: return "Recondition";
    case 248: return "BatterySafe";
    case 252: return "Ext. control";
    default: return "Unknown";
  }
}

bool vic_active_state(uint8_t s) {
  return !(s == 0 || s == 1 || s == 2 || s == 9 || s == 245);
}

const char *vic_alarm_text(uint16_t a) {
  if (a & 0x1000) return "Short circuit";
  if (a & 0x0100) return "Overload";
  if (a & 0x0001) return "Low battery";
  if (a & 0x0002) return "High battery";
  if (a & 0x0040) return "High temp";
  if (a & 0x0020) return "Low temp";
  if (a & 0x0200) return "DC ripple";
  if (a & 0x0400) return "Low AC volt.";
  if (a & 0x0800) return "High AC volt.";
  if (a) return "Alarm";
  return NULL;
}

bool vic_fresh(const VicData &d, uint32_t now) {
  return d.last_seen && now - d.last_seen < VIC_STALE_MS;
}

const char *vic_status(const VicData &d, uint8_t type, uint32_t now) {
  if (!d.last_seen) return "Not heard yet";
  if (now - d.last_seen >= VIC_STALE_MS) return "No signal";
  if (d.key_bad) return "Wrong key";
  if (!vic_supported(type)) return "Type not supported";
  return "OK";
}

void vic_clear_data(VicData &v) {
  memset(&v, 0, sizeof(v));
  v.batt_v = v.batt_i = v.pv_w = v.yield_kwh = NAN;
  v.soc = v.consumed_ah = v.aux_v = v.temp_c = NAN;
  v.in_v = v.out_v = NAN;
  v.ac_va = v.ac_v = v.ac_i = NAN;
  v.remaining_min = -1;
}

/* AES-128-CTR with a 16-bit little-endian start counter (Victron scheme) */
static void vic_decrypt(const uint8_t key[16], uint16_t iv, const uint8_t *in, size_t len, uint8_t *out) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  for (size_t off = 0; off < len; off += 16) {
    uint8_t ctr[16] = { 0 }, ks[16];
    uint32_t c = iv + off / 16;
    ctr[0] = c & 0xFF;
    ctr[1] = (c >> 8) & 0xFF;
    ctr[2] = (c >> 16) & 0xFF;
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, ctr, ks);
    for (size_t i = 0; i < 16 && off + i < len; i++) out[off + i] = in[off + i] ^ ks[i];
  }
  mbedtls_aes_free(&ctx);
}

static void vic_decode(uint8_t type, BitReader &br, VicData &v) {
  if (type == VIC_SOLAR) {
    v.state = br.u(8);
    v.error = br.u(8);
    int32_t bv = br.s(16);
    v.batt_v = (bv == 0x7FFF) ? NAN : bv / 100.0f;
    int32_t bi = br.s(16);
    v.batt_i = (bi == 0x7FFF) ? NAN : bi / 10.0f;
    uint32_t y = br.u(16);
    v.yield_kwh = (y == 0xFFFF) ? NAN : y / 100.0f;
    uint32_t pw = br.u(16);
    v.pv_w = (pw == 0xFFFF) ? NAN : (float)pw;
  } else if (type == VIC_BATTMON) {
    uint32_t rem = br.u(16);
    v.remaining_min = (rem == 0xFFFF) ? -1 : (int)rem;
    int32_t bv = br.s(16);
    v.batt_v = (bv == 0x7FFF) ? NAN : bv / 100.0f;
    br.u(16);  // alarm reason
    uint32_t aux = br.u(16);
    v.aux_mode = br.u(2);
    int32_t cur = br.s(22);
    v.batt_i = (cur == 0x1FFFFF) ? NAN : cur / 1000.0f;
    uint32_t cons = br.u(20);
    v.consumed_ah = (cons == 0xFFFFF) ? NAN : -(cons / 10.0f);
    uint32_t soc = br.u(10);
    v.soc = (soc == 0x3FF) ? NAN : soc / 10.0f;
    if (v.aux_mode == 0 || v.aux_mode == 1) {  // starter / midpoint voltage
      int16_t a = (int16_t)aux;
      if (a != 0x7FFF) v.aux_v = a / 100.0f;
    } else if (v.aux_mode == 2 && aux != 0xFFFF) {  // temperature (Kelvin x100)
      v.temp_c = aux / 100.0f - 273.15f;
    }
  } else if (type == VIC_INVERTER) {
    v.state = br.u(8);
    v.alarm = br.u(16);
    int32_t bv = br.s(16);
    v.batt_v = (bv == 0x7FFF) ? NAN : bv / 100.0f;
    uint32_t va = br.u(16);
    v.ac_va = (va == 0xFFFF) ? NAN : (float)va;
    uint32_t acv = br.u(15);
    v.ac_v = (acv == 0x7FFF) ? NAN : acv / 100.0f;
    uint32_t aci = br.u(11);
    v.ac_i = (aci == 0x7FF) ? NAN : aci / 10.0f;
  } else if (type == VIC_DCDC) {
    v.state = br.u(8);
    v.error = br.u(8);
    uint32_t in = br.u(16);
    v.in_v = (in == 0xFFFF) ? NAN : in / 100.0f;
    int32_t out = br.s(16);
    v.out_v = (out == 0x7FFF) ? NAN : out / 100.0f;
  } else if (type == VIC_ACCHG) {
    v.state = br.u(8);
    v.error = br.u(8);
    uint32_t ov = br.u(13);
    v.batt_v = (ov == 0x1FFF) ? NAN : ov / 100.0f;
    uint32_t oc = br.u(11);
    v.batt_i = (oc == 0x7FF) ? NAN : oc / 10.0f;
    br.u(13);  // output 2 voltage
    br.u(11);  // output 2 current
    br.u(13);  // output 3 voltage
    br.u(11);  // output 3 current
    uint32_t t = br.u(7);
    v.temp_c = (t == 0x7F) ? NAN : (int)t - 40;
  }
}

void vic_handle(const std::string &md, const char *addr, int rssi) {
  const uint8_t *d = (const uint8_t *)md.data();
  size_t n = md.size();
  // company ID 0x02E1 (little endian), then 0x10 = product advertisement
  if (n < 12 || d[0] != 0xE1 || d[1] != 0x02 || d[2] != 0x10) return;
  const uint8_t *p = d + 2;  // [4]=record type [5..6]=counter [7]=key check [8..]=encrypted
  size_t len = n - 2;
  uint8_t type = p[4];
  uint16_t iv = p[5] | (p[6] << 8);
  uint8_t key_check = p[7];

  char mac[18];
  strlcpy(mac, addr, sizeof(mac));
  for (char *c = mac; *c; c++) *c = toupper(*c);
  uint32_t now = millis();

  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  // remember for the "Add" list
  int slot = -1, oldest = 0;
  for (int i = 0; i < MAX_VIC_SEEN; i++) {
    if (vic_seen[i].used && !strcmp(vic_seen[i].mac, mac)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_VIC_SEEN; i++) {
      if (!vic_seen[i].used) {
        slot = i;
        break;
      }
      if (vic_seen[i].last_seen < vic_seen[oldest].last_seen) oldest = i;
    }
    if (slot < 0) slot = oldest;
  }
  vic_seen[slot].used = true;
  vic_seen[slot].type = type;
  strlcpy(vic_seen[slot].mac, mac, sizeof(vic_seen[slot].mac));
  vic_seen[slot].rssi = rssi;
  vic_seen[slot].last_seen = now;

  // is it one of the added devices?
  int idx = -1;
  for (int i = 0; i < MAX_VIC; i++) {
    if (vic_cfg[i].used && !strcmp(vic_cfg[i].mac, mac)) {
      idx = i;
      break;
    }
  }
  uint8_t key[16];
  if (idx >= 0) memcpy(key, vic_cfg[idx].key, 16);
  xSemaphoreGive(vic_mtx);
  if (idx < 0) return;

  VicData v;
  vic_clear_data(v);
  v.type = type;
  v.rssi = rssi;
  v.last_seen = now;

  if (key_check != key[0]) {
    v.key_bad = true;  // wrong key (or the device changed its key)
  } else {
    uint8_t plain[32];
    size_t plen = len - 8;
    if (plen > sizeof(plain)) plen = sizeof(plain);
    vic_decrypt(key, iv, p + 8, plen, plain);
    BitReader br = { plain, plen, 0 };
    vic_decode(type, br, v);
    v.key_ok = true;
  }

  xSemaphoreTake(vic_mtx, portMAX_DELAY);
  if (vic_cfg[idx].used && !strcmp(vic_cfg[idx].mac, mac)) vic_data[idx] = v;
  xSemaphoreGive(vic_mtx);
}

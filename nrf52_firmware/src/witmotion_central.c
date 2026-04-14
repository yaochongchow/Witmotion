#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "witmotion_central.h"

LOG_MODULE_DECLARE(witmotion_relay, LOG_LEVEL_INF);

#define WITMOTION_CONN_INTERVAL_UNITS 6
#define WITMOTION_CONN_TIMEOUT_UNITS  400

#define WITMOTION_RATE_CODE           0x0B
#define RTT_PRINT_EVERY_N_SAMPLES     1U
#define DEG_TO_RAD                    0.01745329251994329577f

struct bt_conn *witmotion_conn;

static struct bt_gatt_subscribe_params subscribe_params;
static struct bt_gatt_discover_params discover_params;

static uint8_t sample_seq;
static uint8_t wt_buf[64];
static size_t wt_buf_len;
static bool rtt_header_printed;

static int8_t scan_rssi;

static int64_t hz_window_start;
static uint16_t hz_window_samples;
static uint8_t measured_hz;

static const struct bt_le_conn_param fast_conn_param = {
    .interval_min = WITMOTION_CONN_INTERVAL_UNITS,
    .interval_max = WITMOTION_CONN_INTERVAL_UNITS,
    .latency = 0,
    .timeout = WITMOTION_CONN_TIMEOUT_UNITS,
};

/* WT901BLE uses 128-bit UUIDs: service FFE5, notify FFE4, write FFE9 */
static struct bt_uuid_128 witmotion_notify_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x0000FFE4, 0x0000, 0x1000, 0x8000, 0x00805F9A34FB));

#define BLACKLIST_MAX 4
static bt_addr_le_t blacklist[BLACKLIST_MAX];
static int blacklist_count;

static int8_t clamp_i8(float v)
{
    if (v > 127.0f) {
        return 127;
    }
    if (v < -127.0f) {
        return -127;
    }
    return (int8_t)lrintf(v);
}

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x8000U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static int16_t to_s16(uint8_t lo, uint8_t hi)
{
    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}

static void euler_to_quat(float roll_deg, float pitch_deg, float yaw_deg,
                          float *qw, float *qx, float *qy, float *qz)
{
    float cr, sr, cp, sp, cy, sy;
    float rr = roll_deg * DEG_TO_RAD;
    float pr = pitch_deg * DEG_TO_RAD;
    float yr = yaw_deg * DEG_TO_RAD;

    cr = cosf(rr * 0.5f);
    sr = sinf(rr * 0.5f);
    cp = cosf(pr * 0.5f);
    sp = sinf(pr * 0.5f);
    cy = cosf(yr * 0.5f);
    sy = sinf(yr * 0.5f);

    *qw = cr * cp * cy + sr * sp * sy;
    *qx = sr * cp * cy - cr * sp * sy;
    *qy = cr * sp * cy + sr * cp * sy;
    *qz = cr * cp * sy - sr * sp * cy;
}

static bool is_blacklisted(const bt_addr_le_t *addr)
{
    for (int i = 0; i < blacklist_count; i++) {
        if (bt_addr_le_eq(addr, &blacklist[i])) {
            return true;
        }
    }
    return false;
}

static void blacklist_add(const bt_addr_le_t *addr)
{
    if (blacklist_count < BLACKLIST_MAX && !is_blacklisted(addr)) {
        bt_addr_le_copy(&blacklist[blacklist_count++], addr);
        char s[BT_ADDR_LE_STR_LEN];

        bt_addr_le_to_str(addr, s, sizeof(s));
        LOG_INF("Blacklisted %s (no FFE4)", s);
    }
}

static void process_witmotion_packet(const uint8_t *p)
{
    float ax = to_s16(p[2], p[3]) / 32768.0f * 16.0f;
    float ay = to_s16(p[4], p[5]) / 32768.0f * 16.0f;
    float az = to_s16(p[6], p[7]) / 32768.0f * 16.0f;
    float wx = to_s16(p[8], p[9]) / 32768.0f * 2000.0f;
    float wy = to_s16(p[10], p[11]) / 32768.0f * 2000.0f;
    float wz = to_s16(p[12], p[13]) / 32768.0f * 2000.0f;
    float roll = to_s16(p[14], p[15]) / 32768.0f * 180.0f;
    float pitch = to_s16(p[16], p[17]) / 32768.0f * 180.0f;
    float yaw = to_s16(p[18], p[19]) / 32768.0f * 180.0f;

    float qw, qx, qy, qz;
    int8_t i_ax, i_ay, i_az;
    int8_t i_wx, i_wy, i_wz;
    int8_t i_qw, i_qx, i_qy, i_qz;
    uint16_t payload_crc;
    uint8_t crc_payload[13];

    int64_t now = k_uptime_get();

    euler_to_quat(roll, pitch, yaw, &qw, &qx, &qy, &qz);

    if (hz_window_start == 0) {
        hz_window_start = now;
        hz_window_samples = 0;
    }
    hz_window_samples++;

    int64_t window_dt = now - hz_window_start;
    if (window_dt >= 200) {
        uint32_t hz_calc = (uint32_t)((hz_window_samples * 1000U) / (uint32_t)window_dt);
        measured_hz = (uint8_t)MIN(hz_calc, 255U);
        hz_window_start = now;
        hz_window_samples = 0;
    }

    i_ax = clamp_i8(ax / 16.0f * 127.0f);
    i_ay = clamp_i8(ay / 16.0f * 127.0f);
    i_az = clamp_i8(az / 16.0f * 127.0f);
    i_wx = clamp_i8(wx / 2000.0f * 127.0f);
    i_wy = clamp_i8(wy / 2000.0f * 127.0f);
    i_wz = clamp_i8(wz / 2000.0f * 127.0f);
    i_qw = clamp_i8(qw * 127.0f);
    i_qx = clamp_i8(qx * 127.0f);
    i_qy = clamp_i8(qy * 127.0f);
    i_qz = clamp_i8(qz * 127.0f);

    crc_payload[0] = sample_seq;
    crc_payload[1] = (uint8_t)i_ax;
    crc_payload[2] = (uint8_t)i_ay;
    crc_payload[3] = (uint8_t)i_az;
    crc_payload[4] = (uint8_t)i_wx;
    crc_payload[5] = (uint8_t)i_wy;
    crc_payload[6] = (uint8_t)i_wz;
    crc_payload[7] = (uint8_t)i_qw;
    crc_payload[8] = (uint8_t)i_qx;
    crc_payload[9] = (uint8_t)i_qy;
    crc_payload[10] = (uint8_t)i_qz;
    crc_payload[11] = (uint8_t)scan_rssi;
    crc_payload[12] = measured_hz;
    payload_crc = crc16_ccitt_false(crc_payload, sizeof(crc_payload));

    if ((sample_seq % RTT_PRINT_EVERY_N_SAMPLES) == 0U) {
        if (!rtt_header_printed) {
            printk("Time(ms) | Seq:# | Acc(x,y,z) | Gyro(x,y,z) | Q(w,x,y,z) | RSSI:dBm | Hz | CRC16\n");
            printk("---------+-------+------------+-------------+-------------+----------+----+------\n");
            rtt_header_printed = true;
        }

        printk("%u | Seq:%u | Acc(%d,%d,%d) | Gyro(%d,%d,%d) | Q(%d,%d,%d,%d) | RSSI:%d | Hz:%u | CRC:%04X\n",
               (uint32_t)now,
               sample_seq,
               i_ax, i_ay, i_az,
               i_wx, i_wy, i_wz,
               i_qw, i_qx, i_qy, i_qz,
               scan_rssi,
               measured_hz,
               payload_crc);
    }

    sample_seq++;
}

static uint8_t witmotion_notify_cb(struct bt_conn *conn,
                                   struct bt_gatt_subscribe_params *params,
                                   const void *data, uint16_t length)
{
    ARG_UNUSED(conn);

    if (!data) {
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    if (wt_buf_len + length > sizeof(wt_buf)) {
        wt_buf_len = 0;
    }

    memcpy(&wt_buf[wt_buf_len], data, length);
    wt_buf_len += length;

    while (wt_buf_len >= 20) {
        size_t s = 0;

        while (s < wt_buf_len && wt_buf[s] != 0x55) {
            s++;
        }
        if (s > 0) {
            memmove(wt_buf, &wt_buf[s], wt_buf_len - s);
            wt_buf_len -= s;
        }

        if (wt_buf_len < 20) {
            break;
        }

        if (wt_buf[1] != 0x61) {
            memmove(wt_buf, &wt_buf[1], wt_buf_len - 1);
            wt_buf_len--;
            continue;
        }

        process_witmotion_packet(wt_buf);
        memmove(wt_buf, &wt_buf[20], wt_buf_len - 20);
        wt_buf_len -= 20;
    }

    return BT_GATT_ITER_CONTINUE;
}

static void send_witmotion_cmd(struct bt_conn *conn, uint16_t value_handle,
                               const uint8_t cmd[5], const char *name)
{
    int err = bt_gatt_write_without_response(conn, value_handle, cmd, 5, false);

    if (err) {
        LOG_WRN("Failed to send %s cmd (err %d)", name, err);
    } else {
        LOG_INF("Sent %s cmd", name);
    }
}

static uint8_t write_discover_cb(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
    ARG_UNUSED(params);

    if (!attr) {
        LOG_INF("FFE9 write characteristic not found");
        return BT_GATT_ITER_STOP;
    }

    uint16_t value_handle = attr->handle + 1;

    static const uint8_t unlock_cmd[] = { 0xFF, 0xAA, 0x69, 0x88, 0xB5 };
    static const uint8_t set_rate_200hz_cmd[] = { 0xFF, 0xAA, 0x03, WITMOTION_RATE_CODE, 0x00 };
    static const uint8_t save_cfg_cmd[] = { 0xFF, 0xAA, 0x00, 0x00, 0x00 };

    send_witmotion_cmd(conn, value_handle, unlock_cmd, "unlock");
    send_witmotion_cmd(conn, value_handle, set_rate_200hz_cmd, "set-rate-200hz");
    send_witmotion_cmd(conn, value_handle, save_cfg_cmd, "save-config");

    return BT_GATT_ITER_STOP;
}

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params)
{
    ARG_UNUSED(params);

    if (!attr) {
        LOG_INF("No FFE4 notify characteristic found, disconnecting");
        struct bt_conn_info ci;

        if (!bt_conn_get_info(conn, &ci)) {
            blacklist_add(ci.le.dst);
        }

        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return BT_GATT_ITER_STOP;
    }

    struct bt_gatt_chrc *chrc = attr->user_data;

    subscribe_params.notify = witmotion_notify_cb;
    subscribe_params.value = BT_GATT_CCC_NOTIFY;
    subscribe_params.value_handle = chrc->value_handle;
    subscribe_params.ccc_handle = chrc->value_handle + 1;

    int err = bt_gatt_subscribe(conn, &subscribe_params);
    if (err) {
        LOG_ERR("Subscribe failed (err %d)", err);
    } else {
        LOG_INF("Subscribed to Witmotion notifications");
    }

    static struct bt_uuid_128 write_uuid = BT_UUID_INIT_128(
        BT_UUID_128_ENCODE(0x0000FFE9, 0x0000, 0x1000, 0x8000, 0x00805F9A34FB));
    static struct bt_gatt_discover_params write_disc;

    write_disc.uuid = &write_uuid.uuid;
    write_disc.func = write_discover_cb;
    write_disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    write_disc.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    write_disc.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    bt_gatt_discover(conn, &write_disc);

    return BT_GATT_ITER_STOP;
}

static void witmotion_discover(struct bt_conn *conn)
{
    discover_params.uuid = &witmotion_notify_uuid.uuid;
    discover_params.func = discover_cb;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    bt_gatt_discover(conn, &discover_params);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        witmotion_conn = NULL;
        return;
    }

    struct bt_conn_info info;

    bt_conn_get_info(conn, &info);

    if (info.role == BT_CONN_ROLE_CENTRAL) {
        witmotion_conn = bt_conn_ref(conn);

        int perr = bt_conn_le_param_update(conn, &fast_conn_param);
        if (perr) {
            LOG_WRN("Conn param update failed (err %d)", perr);
        }

        LOG_INF("Witmotion connected, discovering services...");
        witmotion_discover(conn);
    } else {
        LOG_INF("Client connected");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (conn == witmotion_conn) {
        bt_conn_unref(witmotion_conn);
        witmotion_conn = NULL;
        wt_buf_len = 0;
        LOG_INF("Witmotion disconnected (reason %u)", reason);
    } else {
        LOG_INF("Client disconnected (reason %u)", reason);
    }
}

BT_CONN_CB_DEFINE(conn_cbs) = {
    .connected = connected,
    .disconnected = disconnected,
};

static bool ad_has_uuid_ffe0(struct net_buf_simple *ad)
{
    while (ad->len > 1) {
        uint8_t len = net_buf_simple_pull_u8(ad);
        if (len == 0 || len > ad->len) {
            break;
        }

        uint8_t type = net_buf_simple_pull_u8(ad);
        uint8_t dlen = len - 1;

        if ((type == BT_DATA_UUID16_SOME || type == BT_DATA_UUID16_ALL) && dlen >= 2) {
            for (uint8_t i = 0; i + 1 < dlen; i += 2) {
                uint16_t u = (uint16_t)ad->data[i] | ((uint16_t)ad->data[i + 1] << 8);
                if (u == 0xFFE0 || u == 0xFFE4 || u == 0xFFE5) {
                    return true;
                }
            }
        }

        net_buf_simple_pull(ad, dlen);
    }

    return false;
}

static bool ad_has_name_wt(struct net_buf_simple *ad)
{
    while (ad->len > 1) {
        uint8_t len = net_buf_simple_pull_u8(ad);
        if (len == 0 || len > ad->len) {
            break;
        }

        uint8_t type = net_buf_simple_pull_u8(ad);
        uint8_t dlen = len - 1;

        if ((type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) && dlen >= 2) {
            if ((ad->data[0] == 'W' && ad->data[1] == 'T') ||
                (ad->data[0] == 'w' && ad->data[1] == 't')) {
                return true;
            }

            for (uint8_t i = 0; i + 1 < dlen; i++) {
                if ((ad->data[i] == 'W' || ad->data[i] == 'w') &&
                    (ad->data[i + 1] == 'T' || ad->data[i + 1] == 't')) {
                    return true;
                }
            }
        }

        net_buf_simple_pull(ad, dlen);
    }

    return false;
}

static void scan_recv(const bt_addr_le_t *addr, int8_t rssi,
                      uint8_t adv_type, struct net_buf_simple *buf)
{
    ARG_UNUSED(adv_type);

    if (witmotion_conn) {
        return;
    }

    struct net_buf_simple_state state;

    net_buf_simple_save(buf, &state);
    bool has_uuid = ad_has_uuid_ffe0(buf);
    net_buf_simple_restore(buf, &state);
    bool has_name = ad_has_name_wt(buf);
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    bool known_addr = strstr(addr_str, "FE:64:4D:CE:D2:BF") != NULL;

    if (!has_uuid && !has_name && !known_addr) {
        return;
    }

    if (is_blacklisted(addr)) {
        return;
    }

    scan_rssi = rssi;

    LOG_INF("Auto-detected Witmotion: %s (RSSI %d)", addr_str, scan_rssi);

    bt_le_scan_stop();

    struct bt_conn *conn;
    int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                                &fast_conn_param, &conn);
    if (err) {
        LOG_ERR("Connect failed (err %d)", err);
        return;
    }

    bt_conn_unref(conn);
}

void witmotion_scan_init(void)
{
    /* No-op: using direct callback form in bt_le_scan_start(). */
}

void witmotion_scan_start(void)
{
    int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_recv);

    if (err && err != -EALREADY) {
        LOG_WRN("Scan start failed (err %d)", err);
    }
}

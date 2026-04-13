/*
 * Firmware entrypoint for the Witmotion BLE relay application.
 *
 * Runtime model:
 * 1) Enable the Bluetooth stack.
 * 2) Advertise this board as WT_RELAY (peripheral role for downstream clients).
 * 3) Start and maintain active scanning (central role) for a Witmotion source.
 * 4) Keep retrying scan start at a short interval so reconnects are quick after
 *    disconnects or failed connection attempts.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include "witmotion_central.h"

LOG_MODULE_REGISTER(witmotion_relay, LOG_LEVEL_INF);

/* Retry period for restarting scan attempts when no Witmotion is connected. */
#define SCAN_RETRY_INTERVAL_MS 500

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_NAME_COMPLETE, 'W', 'T', '_', 'R', 'E', 'L', 'A', 'Y'),
};

int main(void)
{
    /* Bring up Zephyr Bluetooth host/controller stack. */
    int err = bt_enable(NULL);
    if (err) { LOG_ERR("BT init failed (err %d)", err); return err; }

    /* Register central-side scan callback logic (implemented in witmotion_central.c). */
    witmotion_scan_init();

    /* Start advertising so external clients can subscribe to relay payloads. */
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) { LOG_ERR("Adv start failed (err %d)", err); return err; }
    LOG_INF("Advertising as WT_RELAY, scanning for Witmotion...");

    for (;;) {
        /*
         * Maintain central scan. witmotion_scan_start() is safe to call repeatedly;
         * once connected, central state gates further scan attempts.
         */
        if (!witmotion_conn) {
            witmotion_scan_start();
        }
        k_sleep(K_MSEC(SCAN_RETRY_INTERVAL_MS));
    }
}

/*
 * Firmware entrypoint for the Witmotion BLE relay application.
 *
 * Runtime model:
 * 1) Enable the Bluetooth stack.
 * 2) Start and maintain active scanning (central role) for a Witmotion source.
 * 3) Keep retrying scan start at a short interval so reconnects are quick after
 *    disconnects or failed connection attempts.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <SEGGER_RTT.h>
#include "witmotion_central.h"

LOG_MODULE_REGISTER(witmotion_relay, LOG_LEVEL_INF);

/* Retry period for restarting scan attempts when no Witmotion is connected. */
#define SCAN_RETRY_INTERVAL_MS 500

int main(void)
{
    SEGGER_RTT_Init();

    /* Bring up Zephyr Bluetooth host/controller stack. */
    int err = bt_enable(NULL);
    if (err) { LOG_ERR("BT init failed (err %d)", err); return err; }

    /* Register central-side scan callback logic (implemented in witmotion_central.c). */
    witmotion_scan_init();

    LOG_INF("Scanning for Witmotion...");

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

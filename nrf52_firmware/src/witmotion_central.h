#ifndef WITMOTION_CENTRAL_H_
#define WITMOTION_CENTRAL_H_

#include <zephyr/bluetooth/conn.h>

extern struct bt_conn *witmotion_conn;

void witmotion_scan_init(void);
void witmotion_scan_start(void);

#endif /* WITMOTION_CENTRAL_H_ */

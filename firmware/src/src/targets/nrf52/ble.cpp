/*
 * This file is part of the Head Tracker distribution (https://github.com/dlktdr/headtracker)
 * Copyright (c) 2021 Cliff Blackburn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <zephyr.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>

#include "log.h"
#include "io.h"
#include "nano33ble.h"
#include "ble.h"

// Globals
volatile bool bleconnected=false;
volatile bool btscanonly = false;
btmodet curmode = BTDISABLE;

// UUID's
struct bt_uuid_16 ccc = BT_UUID_INIT_16(0x2902);
struct bt_uuid_16 frskyserv = BT_UUID_INIT_16(0xFFF0);
struct bt_uuid_16 frskychar = BT_UUID_INIT_16(0xFFF6);
struct bt_uuid_16 htoverridech = BT_UUID_INIT_16(0xAFF1);
struct bt_uuid_16 btbutton = BT_UUID_INIT_16(0xAFF2);
struct bt_uuid_16 jsonuuid = BT_UUID_INIT_16(0xAFF3);

// Switching modes, don't execute
volatile bool btThreadRun = false;

void bt_init()
{
    int err = bt_enable(NULL);
	if (err) {
		LOGE("Bluetooth init failed (err %d)", err);
		return;
	}

  LOGI("Bluetooth initialized");
  btThreadRun = true;
}

void bt_Thread()
{
  int64_t usduration=0;
  while(1) {
    usduration = micros64();

    if(!btThreadRun) {
      rt_sleep_ms(10);
      continue;
    }

    switch(curmode) {
    case BTPARAHEAD:
      BTHeadExecute();
      break;
    case BTPARARMT:
      BTRmtExecute();
      break;
    case BTSCANONLY:
      break;
    default:
      break;
    }

    if(bleconnected)
      setLEDFlag(LED_BTCONNECTED);
    else
      clearLEDFlag(LED_BTCONNECTED);

    // Adjust sleep for a more accurate period
    usduration = micros64() - usduration;
    if(BT_PERIOD - usduration < BT_PERIOD * 0.7) {  // Took a long time. Will crash if sleep is too short
      rt_sleep_us(BT_PERIOD);
    } else {
      rt_sleep_us(BT_PERIOD - usduration);
    }
  }
}

void BTSetMode(btmodet mode)
{
    // Requested same mode, just return
    if(mode == curmode)
        return;

    btThreadRun = false;

    // Shut Down
    switch(curmode) {
    case BTPARAHEAD:
        BTHeadStop();
        break;
    case BTPARARMT:
        BTRmtStop();
        btscanonly = false;
        break;
    case BTSCANONLY:
        BTRmtStop();
        btscanonly = false;
        break;
    default:
        break;
    }

    // Start Up
    switch(mode) {
    case BTPARAHEAD:
        BTHeadStart();
        break;
    case BTPARARMT:
        btscanonly = false;
        BTRmtStart();
        break;
    case BTSCANONLY:
        btscanonly = true;
        BTRmtStart();
    default:
        break;
    }

    btThreadRun = true;

    curmode = mode;
}

btmodet BTGetMode()
{
    return curmode;
}

bool BTGetConnected()
{
    if(curmode == BTDISABLE)
        return false;
    return bleconnected;
}

uint16_t BTGetChannel(int chno)
{
    switch(curmode) {
    case BTPARAHEAD:
        return BTHeadGetChannel(chno);
    case BTPARARMT:
        return BTRmtGetChannel(chno);
    case BTSCANONLY:
        return 0;
    default:
        break;
    }

    return 0;
}

void BTSetChannel(int channel, const uint16_t value)
{
    switch(curmode) {
    case BTPARAHEAD:
        BTHeadSetChannel(channel, value);
        break;
    case BTPARARMT:
        BTRmtSetChannel(channel, value);
        break;
    default:
        break;
    }
}

const char *BTGetAddress()
{
    switch(curmode) {
    case BTPARAHEAD:
        return BTHeadGetAddress();
    case BTPARARMT:
    case BTSCANONLY:
        return BTRmtGetAddress();
    default:
        break;
    }
    return "BT_DISABLED";
}

int8_t BTGetRSSI()
{
    switch(curmode) {
    case BTPARAHEAD:
        return BTHeadGetRSSI();
    case BTPARARMT:
        return BTRmtGetRSSI();
        break;
    default:
        break;
    }

    return -1;
}

bool leparamrequested(struct bt_conn *conn, struct bt_le_conn_param *param)
{
  LOGI("Bluetooth Params Request. IntMax:%d IntMin:%d Lat:%d Timeout:%d",
    param->interval_max,
    param->interval_min,
    param->latency,
    param->timeout);
  return true;
}

void leparamupdated(struct bt_conn *conn,
                           uint16_t interval,
				                   uint16_t latency,
                           uint16_t timeout)
{
  LOGI("Bluetooth Params Updated. Int:%d Lat:%d Timeout:%d", interval, latency, timeout);
}

void securitychanged(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
  LOGI("Bluetooth Security Changed. Lvl:%d Err:%d", level, err);
}

const char *printPhy(int phy)
{
  switch(phy) {
    case BT_GAP_LE_PHY_NONE:
      return("None");
    case BT_GAP_LE_PHY_1M:
      return("1M");
    case BT_GAP_LE_PHY_2M:
      return("2M");
    case BT_GAP_LE_PHY_CODED:
      return("Coded");
  }
  return "Unknown";
}

void lephyupdated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
  LOGI("Bluetooth PHY Updated. RxPHY:%s TxPHY:%s", param->rx_phy, param->tx_phy);
}

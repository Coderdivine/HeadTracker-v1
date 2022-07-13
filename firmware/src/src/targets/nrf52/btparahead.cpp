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
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include "trackersettings.h"
#include "btparahead.h"
#include "log.h"
#include "io.h"
#include "nano33ble.h"
#include "defines.h"

void sendTrainer();
int setTrainer(uint8_t *addr);
void pushByte(uint8_t byte);

static void disconnected(struct bt_conn *conn, uint8_t reason);
static void connected(struct bt_conn *conn, uint8_t err);
static ssize_t write_ct(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t read_ct(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset);
static ssize_t write_json(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t read_json(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset);
static ssize_t write_but(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t read_over(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset);
static void ct_ccc_cfg_changed_frsky(const struct bt_gatt_attr *attr, uint16_t value);
static void ct_ccc_cfg_changed_overr(const struct bt_gatt_attr *attr, uint16_t value);

static constexpr uint8_t START_STOP = 0x7E;
static constexpr uint8_t BYTE_STUFF = 0x7D;
static constexpr uint8_t STUFF_MASK = 0x20;
static uint8_t buffer[BLUETOOTH_LINE_LENGTH+1];
static uint16_t chan_vals[BT_CHANNELS];
static uint8_t bufferIndex;
static uint8_t crc;
static uint8_t ct[40];
static uint8_t overdata[2];
static char _address[18] = "00:00:00:00:00:00";
uint16_t ovridech = 0xFFFF;

// JSON Ring Buffer Reader
DynamicJsonDocument blejson(JSON_BUF_SIZE);
char blejsonbuffer[JSON_BUF_SIZE] = "";
char *blejsonbufptr = blejsonbuffer;

// Service UUID
static struct bt_uuid_128 btparaserv = BT_UUID_INIT_128(
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10,	0x00, 0x00, 0xf0, 0xff, 0x00, 0x00);


BT_GATT_SERVICE_DEFINE(bt_srv,
    // ATTRIBUTE 0
    BT_GATT_PRIMARY_SERVICE(&btparaserv),

    // Data output Characteristic  ATTRIBUTE 1,2
    BT_GATT_CHARACTERISTIC(&frskychar.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP |  BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_ct, write_ct, ct),
    // ATTRIBUTE 3
    BT_GATT_CCC(ct_ccc_cfg_changed_frsky, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    // Overridden Channel Outputs ATTRIBUTE 4,5
    BT_GATT_CHARACTERISTIC(&htoverridech.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ , read_over, NULL, overdata),
    // ATTRIBUTE 6
    BT_GATT_CCC(ct_ccc_cfg_changed_overr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    // Remote Button Press Characteristic, Indicate ATTRIBUTE 7
    BT_GATT_CHARACTERISTIC(&btbutton.uuid,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL, write_but, NULL),

    // Remote Button Press Characteristic, Indicate ATTRIBUTE 8
    BT_GATT_CHARACTERISTIC(&jsonuuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_json, write_json, NULL),

    );

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_SOME, BT_UUID_16_ENCODE(0xFFF0)),
#if defined(BT_MOD_CC2540)
    BT_DATA_BYTES(0x12, 0x00, 0x60, 0x00, 0x60),
#endif
};

static struct bt_le_adv_param my_param = {
        .id = BT_ID_DEFAULT, \
        .sid = 0, \
        .secondary_max_skip = 0, \
        .options = (BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_CODED), \
        .interval_min = (BT_GAP_ADV_FAST_INT_MIN_2), \
        .interval_max = (BT_GAP_ADV_FAST_INT_MAX_2), \
        .peer = (NULL), \
    };

struct bt_conn *curconn = NULL;
struct bt_le_conn_param *conparms = BT_LE_CONN_PARAM(BT_MIN_CONN_INTER_PERIF,
                                                     BT_MAX_CONN_INTER_PERIF,
                                                     0,
                                                     BT_CONN_LOST_TIME);

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
  .le_param_req = leparamrequested,
  .le_param_updated = leparamupdated,
  .le_phy_updated = lephyupdated,
  //.security_changed = securitychanged
};

static struct bt_conn_le_phy_param phy_params = {
  .options = BT_CONN_LE_PHY_OPT_CODED_S8,
  .pref_tx_phy = BT_GAP_LE_PHY_CODED,
  .pref_rx_phy = BT_GAP_LE_PHY_CODED,
};

bt_addr_le_t addrarry[CONFIG_BT_ID_MAX];
size_t addrcnt=1;

void BTHeadStart()
{
    bleconnected = false;

    // Center all Channels
    for(int i=0;i < BT_CHANNELS;i++) {
        chan_vals[i] = TrackerSettings::PPM_CENTER;
    }

    LOGI("BLE Starting Head Bluetooth");

    // Start Advertising
    int err = bt_le_adv_start(&my_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOGE("Advertising failed to start (err %d)", err);
		return;
	}

    LOGI("BLE Started Advertising");

    bt_conn_cb_register(&conn_callbacks);


    // Discover BT Address
    bt_id_get(addrarry, &addrcnt);
    if(addrcnt > 0)
        bt_addr_le_to_str(&addrarry[0],_address,sizeof(_address));

    crc = 0;
    bufferIndex = 0;
}

void BTHeadStop()
{
    LOGI("BLE Stopping Head Bluetooth");

    // Stop Advertising
    int rv = bt_le_adv_stop();
    if(rv) {
        LOGE("BLE Unable to Stop advertising");
    } else {
        LOGI("BLE Stopped Advertising");
    }

    if(curconn){
        LOGI("BLE Disconnecting Active Connection");
        bt_conn_disconnect(curconn,0);
        bt_conn_unref(curconn);
    }
    curconn = NULL;

    bleconnected = false;
}

void BTHeadExecute()
{
    if(bleconnected) {
        // Send Trainer Data
        uint8_t output[BLUETOOTH_LINE_LENGTH+1];
        int len;
        len = setTrainer(output);

        bt_gatt_notify(NULL, &bt_srv.attrs[1], output, len);
    }
}

const char * BTHeadGetAddress()
{
    return _address;
}

bool BTHeadGetConnected()
{
    return(bleconnected);
}

void BTHeadSetChannel(int channel, const uint16_t value)
{
    static uint16_t lovch = 0xFFFF;

    if(channel >= BT_CHANNELS)
        return;

    // If channel disabled, make a note for overriden characteristic
    // Actually send it at center so PARA still works
    if(value == 0) {
        ovridech &= ~(1u<<channel);
        chan_vals[channel] = TrackerSettings::PPM_CENTER;

    // Otherwise set the value and set that it is valid
    } else {
        ovridech |= 1u <<channel;
        chan_vals[channel] = value;
    }

    // Send a notify if override ch's have changed
    if(lovch != ovridech) {
        LOGI("Updating Notify Channels");
        bt_gatt_notify(NULL, &bt_srv.attrs[4], &ovridech, 2);
    }
    lovch = ovridech;
}

// Head BT does not return BT data
uint16_t BTHeadGetChannel(int channel)
{
    return 0;
}

int8_t BTHeadGetRSSI()
{
    // *** Implement
    return -1;
}

static void ct_ccc_cfg_changed_overr(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOGI("Override CCC Value Changed (%d)", value);
}

static void ct_ccc_cfg_changed_frsky(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOGI("FrSky CCC Value Changed (%d)", value);
}

static ssize_t read_ct(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len, uint16_t offset)
{
	char *value = (char*)attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(ct));
}

static ssize_t write_ct(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			const void *buf, uint16_t len, uint16_t offset,
			uint8_t flags)
{

	return len;
}


static ssize_t read_json(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len, uint16_t offset)
{
    LOGI("JSON Read");
    JsonVariant v,v1,v2;
	char *value = (char*)attr->user_data;


	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(ct));
}



static ssize_t write_json(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			const void *buf, uint16_t len, uint16_t offset,
			uint8_t flags)
{
    char sc=0;

    LOGI("BLE:%.*s", len, buf);

    for(int i=0; i < len; i++) {
        sc = *((char*)buf + i);
        // Start Of Text Character, clear buffer
        if(sc == 0x02) {
            // Reset Buffer
            blejsonbufptr = blejsonbuffer + 1;
            blejsonbuffer[0] = 0x02; // Save SOT

        // End of Text Characher, parse JSON data
        } else if (sc == 0x03) {
            // Make sure it's a complete frame, SOT at beginning
            if(blejsonbuffer[0] == 0x02) {
                *blejsonbufptr = 0; // Null terminate
                LOGI("BLE Data RX:%s", blejsonbuffer);
                JSON_Process(blejsonbuffer+1);
            }
            // Reset Buffer
            blejsonbufptr = blejsonbuffer;
            blejsonbuffer[0] = 0;
        } else {
            // Check how much free data is in the buffer
            if(blejsonbufptr >= blejsonbuffer + sizeof(blejsonbuffer) - 3) {
                LOGE("JSON data too long, overflow");
                blejsonbufptr = blejsonbuffer; // Reset Buffer
                blejsonbuffer[0] = 0;
            // Add data to buffer
            } else {
                *(blejsonbufptr++) = sc;
            }
        }
    }

	return len;
}


static ssize_t read_over(struct bt_conn *conn, const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len, uint16_t offset)
{
	char *value = (char*)attr->user_data;

    LOGI("Override Ch's Read");
    memcpy(overdata, (void*)&ovridech, sizeof(ovridech));

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(overdata));
}

static ssize_t write_but(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			const void *buf, uint16_t len, uint16_t offset,
			uint8_t flags)
{
    if(len == 1) {
        char ccenet = ((const char*)buf)[0];
        if(ccenet == 'R') {
            LOGI("Remote BT Button Pressed");
            pressButton();
        } else if(ccenet == 'L') {
            LOGI("Remote BT Button Long Pressed");
            longPressButton();
        }
    }
	return len;
}

void hasSecurityChangedTimer(struct k_timer *tmr)
{
    k_timer_stop(tmr);

    if(!curconn)
        return;

    bt_security_t sl = bt_conn_get_security(curconn);

    // If a CC2540 device, is should have changed the security level to 2 by now
    // If you force the notify subscription on a CC2540 right away it won't send data
    if(sl == BT_SECURITY_L1) {
        uint8_t ccv = BT_GATT_CCC_NOTIFY;
        bt_gatt_attr_write_ccc(curconn, &bt_srv.attrs[3], &ccv, 1 , 0, 0);
        LOGI("Detected a CC2650 Chip (PARA Wireless)");
    }
    else
        LOGI("Detected a CC2540 Chip (non-PARA)");
}

K_TIMER_DEFINE(my_timer, hasSecurityChangedTimer, NULL);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOGE("Bluetooth Connection failed %d", err);
	} else {
		LOGI("Bluetooth connected :)");
	}

    // Stop Advertising
    bt_le_adv_stop();

    curconn = bt_conn_ref(conn);

    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);

    char addr_str[50];
    bt_addr_le_to_str(info.le.dst, addr_str, sizeof(addr_str));

    LOGI("Connected to Address %s", addr_str);

    bt_conn_info info2;
    bt_conn_get_info(conn, &info2);
    LOGI("PHY Connection Rx:%s TX:%s", printPhy(info2.le.phy->rx_phy), printPhy(info2.le.phy->tx_phy));
 
    // Set Connection Parameters - Request updated rate
    bt_conn_le_param_update(curconn,conparms);

    LOGI("Requesting coded PHY - %s", bt_conn_le_phy_update(curconn, &phy_params) ? "FAILED" : "Success");

    // Start a Timer, If we don't see a Security Change within this time
    // e.g. a CC2540 chip then force a subscription for the PARA chip
    k_timer_start(&my_timer, K_SECONDS(2), K_SECONDS(0));

    bleconnected = true;
}


static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOGW("Bluetooth disconnected (reason %d)", reason);

    // Start advertising
    int err = bt_le_adv_start(&my_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOGE("Advertising failed to start (err %d)", err);
		return;
	}

    if(curconn)
        bt_conn_unref(curconn);

    curconn = NULL;
    bleconnected = false;
}

// Part of setTrainer to calculate CRC
// From OpenTX

void pushByte(uint8_t byte)
{
    crc ^= byte;
    if (byte == START_STOP || byte == BYTE_STUFF) {
        buffer[bufferIndex++] = BYTE_STUFF;
        byte ^= STUFF_MASK;
    }
    buffer[bufferIndex++] = byte;
}

/* Builds Trainer Data
*     Returns the length of the encoded PPM + CRC
*     Data saved into addr pointer
*/
int setTrainer(uint8_t *addr)
{
    // Allocate Channel Mappings, Set Default to all Center
    uint8_t * cur = buffer;
    bufferIndex = 0;
    crc = 0x00;

    buffer[bufferIndex++] = START_STOP; // start byte
    pushByte(0x80); // trainer frame type?
    for (int channel=0; channel < BT_CHANNELS; channel+=2, cur+=3) {
        uint16_t channelValue1 = chan_vals[channel];
        uint16_t channelValue2 = chan_vals[channel+1];

        pushByte(channelValue1 & 0x00ff);
        pushByte(((channelValue1 & 0x0f00) >> 4) + ((channelValue2 & 0x00f0) >> 4));
        pushByte(((channelValue2 & 0x000f) << 4) + ((channelValue2 & 0x0f00) >> 8));
    }

    buffer[bufferIndex++] = crc;
    buffer[bufferIndex++] = START_STOP; // end byte

    // Copy data to array
    memcpy(addr,buffer,bufferIndex);

    return bufferIndex;
}
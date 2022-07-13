/*
 * This file is part of the Head Tracker distribution (https://github.com/dlktdr/headtracker)
 * Copyright (c) 2022 Cliff Blackburn
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
#include <string.h>
#include <sys/ring_buffer.h>
#include "sbus.h"
#include "auxserial.h"
#include "defines.h"
#include "io.h"
#include "log.h"
#include "trackersettings.h"
#include <nrfx.h>
#include <nrfx_uarte.h>

#define SBUS_FRAME_LEN   25

static constexpr uint8_t HEADER_ = 0x0F;
static constexpr uint8_t FOOTER_ = 0x00;
static constexpr uint8_t FOOTER2_ = 0x04;
static constexpr uint8_t LEN_ = 25;
static constexpr uint8_t CH17_ = 0x01;
static constexpr uint8_t CH18_ = 0x02;
static constexpr uint8_t LOST_FRAME_ = 0x04;
static constexpr uint8_t FAILSAFE_ = 0x08;
static constexpr uint8_t CH17_MASK_ = 0x01;
static constexpr uint8_t CH18_MASK_ = 0x02;
static constexpr uint8_t LOST_FRAME_MASK_ = 0x04;
static constexpr uint8_t FAILSAFE_MASK_ = 0x08;
static bool failsafe_ = false, lost_frame_ = false, ch17_ = false, ch18_ = false;

volatile bool sbusTreadRun=false;
volatile bool sbusBuildingData=false;
volatile bool sbusoutinv=false;
volatile bool sbusininv=false;
volatile bool sbusinsof=false; // Start of Frame

uint8_t localTXBuffer[SBUS_FRAME_LEN]; // Local Buffer

void sbus_Thread()
{
    while(1) {
        if(!sbusTreadRun){
            rt_sleep_ms(50);
            continue;
        }
        rt_sleep_us((1.0/(float)trkset.SBUSRate()) * 1.0e6);

        // Has the SBUS inverted status changed
        if(sbusoutinv != !trkset.invertedSBUSOut() ||
           sbusininv != !trkset.invertedSBUSIn()) {
            sbusininv = !trkset.invertedSBUSIn();
            sbusoutinv = !trkset.invertedSBUSOut();

            // Close and re-open port with new settings
            AuxSerial_Close();
            uint8_t inversion =0;
            if(sbusininv) inversion |= CONFINV_RX;
            if(sbusoutinv) inversion |= CONFINV_TX;
            AuxSerial_Open(BAUD100000, CONF8E2, inversion);
        }
        // Send SBUS Data
        SBUS_TX_Start();
    }
}

uint8_t buf_[SBUS_FRAME_LEN];
int bytesfilled=0;
int8_t state_ = 0;
uint8_t prev_byte_ = FOOTER_;
uint8_t cur_byte_;

#ifdef DEBUG
uint64_t bytecount=0;
#endif
bool SbusRx_Parse() {
  /* Parse messages */
  while (AuxSerial_Read(&cur_byte_, 1)) {
      /*TODO fixme serialWriteHex(&cur_byte_,1);
      if ((cur_byte_ == HEADER_) && ((prev_byte_ == FOOTER_) ||
         ((prev_byte_ & 0x0F) == FOOTER2_))) {
        serialWriteln();
         }*/
#ifdef DEBUG
      bytecount++;
#endif
     if (state_ == 0) {
      if ((cur_byte_ == HEADER_) && ((prev_byte_ == FOOTER_) ||
         ((prev_byte_ & 0x0F) == FOOTER2_))) {
        buf_[state_++] = cur_byte_;
      } else {
        state_ = 0;
      }
    } else {
      if (state_ < SBUS_FRAME_LEN) {
        buf_[state_++] = cur_byte_;
      } else {
        state_ = 0;
        if ((buf_[SBUS_FRAME_LEN - 1] == FOOTER_) ||
           ((buf_[SBUS_FRAME_LEN - 1] & 0x0F) == FOOTER2_)) {
          return true;
        } else {
          return false;
        }
      }
    }
    prev_byte_ = cur_byte_;
  }
  return false;
}

#ifdef DEBUG
uint8_t sbusrate=0;
uint64_t sbstarttime = 0;
uint64_t bytesread = 0;
#endif

/* FROM -----
 * Brian R Taylor
 * brian.taylor@bolderflight.com
 *
 * Copyright (c) 2021 Bolder Flight Systems Inc
 */

bool SBUS_Read_Data(uint16_t ch_[16])
{
#ifdef DEBUG
        static bool toggle=false;
        pinMode(D_TO_32X_PIN(8),GPIO_OUTPUT);
        digitalWrite(D_TO_32X_PIN(8),toggle);
        toggle = !toggle;
#endif
    bool newdata = false;
    while(SbusRx_Parse()) { // Get most recent data if more than 1 packet came in
        newdata = true;
    }
    if (newdata) {
        ch_[0]  = static_cast<int16_t>(buf_[1]       | ((buf_[2]  << 8) & 0x07FF));
        ch_[1]  = static_cast<int16_t>(buf_[2]  >> 3 | ((buf_[3]  << 5) & 0x07FF));
        ch_[2]  = static_cast<int16_t>(buf_[3]  >> 6 | ((buf_[4]  << 2)  | ((buf_[5] << 10) & 0x07FF)));
        ch_[3]  = static_cast<int16_t>(buf_[5]  >> 1 | ((buf_[6]  << 7) & 0x07FF));
        ch_[4]  = static_cast<int16_t>(buf_[6]  >> 4 | ((buf_[7]  << 4) & 0x07FF));
        ch_[5]  = static_cast<int16_t>(buf_[7]  >> 7 | ((buf_[8]  << 1)  | ((buf_[9] << 9) & 0x07FF)));
        ch_[6]  = static_cast<int16_t>(buf_[9]  >> 2 | ((buf_[10] << 6) & 0x07FF));
        ch_[7]  = static_cast<int16_t>(buf_[10] >> 5 | ((buf_[11] << 3) & 0x07FF));
        ch_[8]  = static_cast<int16_t>(buf_[12]      | ((buf_[13] << 8) & 0x07FF));
        ch_[9]  = static_cast<int16_t>(buf_[13] >> 3 | ((buf_[14] << 5) & 0x07FF));
        ch_[10] = static_cast<int16_t>(buf_[14] >> 6 | ((buf_[15] << 2)  | ((buf_[16] << 10) & 0x07FF)));
        ch_[11] = static_cast<int16_t>(buf_[16] >> 1 | ((buf_[17] << 7) & 0x07FF));
        ch_[12] = static_cast<int16_t>(buf_[17] >> 4 | ((buf_[18] << 4) & 0x07FF));
        ch_[13] = static_cast<int16_t>(buf_[18] >> 7 | ((buf_[19] << 1)  | ((buf_[20] << 9) & 0x07FF)));
        ch_[14] = static_cast<int16_t>(buf_[20] >> 2 | ((buf_[21] << 6) & 0x07FF));
        ch_[15] = static_cast<int16_t>(buf_[21] >> 5 | ((buf_[22] << 3) & 0x07FF));
        for(int i=0; i < 16; i++) { // Shift + Scale SBUS to PPM Range
            ch_[i] = (((float)ch_[i] - TrackerSettings::SBUS_CENTER) / TrackerSettings::SBUS_SCALE) + TrackerSettings::PPM_CENTER;
            if(ch_[i] > TrackerSettings::MAX_PWM) ch_[i] = TrackerSettings::MAX_PWM;
            if(ch_[i] < TrackerSettings::MIN_PWM) ch_[i] = TrackerSettings::MIN_PWM;
        }

#ifdef DEBUG
        static bool toggle=false;
        pinMode(D_TO_32X_PIN(7),GPIO_OUTPUT);
        digitalWrite(D_TO_32X_PIN(7),toggle);
        toggle = !toggle;

        if(sbusrate++ == 0) {
            sbstarttime = millis64(); // Store start time
            bytesread = bytecount;

        } else if(sbusrate == 100) { // After 100 samples, output the time taken
            float elapsed = (float)(millis64() - sbstarttime) / 1000.0f;
            uint32_t bytes = bytecount - bytesread; // Bytes read in this time
            sbusrate = 0;
            LOGD("SBUS Rate - %d BytesRx - %d", (int)(elapsed * 1000.0f), (int)bytes);
        }
#endif

        return true;
    }

    return false;
}

void SBUS_TX_Start()
{
    if(sbusBuildingData)
        return;
    AuxSerial_Write(localTXBuffer,SBUS_FRAME_LEN);
}

void sbus_init()
{
    sbusininv = !trkset.invertedSBUSIn();
    sbusoutinv = !trkset.invertedSBUSOut();
    uint8_t inversion =0;
    if(sbusininv) inversion |= CONFINV_RX;
    if(sbusoutinv) inversion |= CONFINV_TX;
    AuxSerial_Open(BAUD100000, CONF8E2, inversion);
    sbusTreadRun = true;
}

// Build Channel Data

/* FROM -----
 * Brian R Taylor
 * brian.taylor@bolderflight.com
 *
 * Copyright (c) 2021 Bolder Flight Systems Inc
 */


void SBUS_TX_BuildData(uint16_t ch_[16])
{
    sbusBuildingData = true;
    uint8_t *buf_ = localTXBuffer;
    buf_[0] = HEADER_;
    buf_[1] =  static_cast<uint8_t>((ch_[0]  & 0x07FF));
    buf_[2] =  static_cast<uint8_t>((ch_[0]  & 0x07FF) >> 8  |
                (ch_[1]  & 0x07FF) << 3);
    buf_[3] =  static_cast<uint8_t>((ch_[1]  & 0x07FF) >> 5  |
                (ch_[2]  & 0x07FF) << 6);
    buf_[4] =  static_cast<uint8_t>((ch_[2]  & 0x07FF) >> 2);
    buf_[5] =  static_cast<uint8_t>((ch_[2]  & 0x07FF) >> 10 |
                (ch_[3]  & 0x07FF) << 1);
    buf_[6] =  static_cast<uint8_t>((ch_[3]  & 0x07FF) >> 7  |
                (ch_[4]  & 0x07FF) << 4);
    buf_[7] =  static_cast<uint8_t>((ch_[4]  & 0x07FF) >> 4  |
                (ch_[5]  & 0x07FF) << 7);
    buf_[8] =  static_cast<uint8_t>((ch_[5]  & 0x07FF) >> 1);
    buf_[9] =  static_cast<uint8_t>((ch_[5]  & 0x07FF) >> 9  |
                (ch_[6]  & 0x07FF) << 2);
    buf_[10] = static_cast<uint8_t>((ch_[6]  & 0x07FF) >> 6  |
                (ch_[7]  & 0x07FF) << 5);
    buf_[11] = static_cast<uint8_t>((ch_[7]  & 0x07FF) >> 3);
    buf_[12] = static_cast<uint8_t>((ch_[8]  & 0x07FF));
    buf_[13] = static_cast<uint8_t>((ch_[8]  & 0x07FF) >> 8  |
                (ch_[9]  & 0x07FF) << 3);
    buf_[14] = static_cast<uint8_t>((ch_[9]  & 0x07FF) >> 5  |
                (ch_[10] & 0x07FF) << 6);
    buf_[15] = static_cast<uint8_t>((ch_[10] & 0x07FF) >> 2);
    buf_[16] = static_cast<uint8_t>((ch_[10] & 0x07FF) >> 10 |
                (ch_[11] & 0x07FF) << 1);
    buf_[17] = static_cast<uint8_t>((ch_[11] & 0x07FF) >> 7  |
                (ch_[12] & 0x07FF) << 4);
    buf_[18] = static_cast<uint8_t>((ch_[12] & 0x07FF) >> 4  |
                (ch_[13] & 0x07FF) << 7);
    buf_[19] = static_cast<uint8_t>((ch_[13] & 0x07FF) >> 1);
    buf_[20] = static_cast<uint8_t>((ch_[13] & 0x07FF) >> 9  |
                (ch_[14] & 0x07FF) << 2);
    buf_[21] = static_cast<uint8_t>((ch_[14] & 0x07FF) >> 6  |
                (ch_[15] & 0x07FF) << 5);
    buf_[22] = static_cast<uint8_t>((ch_[15] & 0x07FF) >> 3);
    buf_[23] = 0x00 | (ch17_ * CH17_) | (ch18_ * CH18_) |
                (failsafe_ * FAILSAFE_) | (lost_frame_ * LOST_FRAME_);
    buf_[24] = FOOTER_;
    sbusBuildingData = false;
}


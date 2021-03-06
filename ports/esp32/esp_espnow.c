/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Nick Moore
 * Copyright (c) 2018 shawwwn <shawwwn1@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/nlr.h"
#include "py/objstr.h"
#include "py/objlist.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"

#include "modnetwork.h"

NORETURN void _esp_espnow_exceptions(esp_err_t e) {
   switch (e) {
      case ESP_ERR_ESPNOW_NOT_INIT:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Not Initialized");
      case ESP_ERR_ESPNOW_ARG:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Invalid Argument");
      case ESP_ERR_ESPNOW_NO_MEM:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Out Of Mem");
      case ESP_ERR_ESPNOW_FULL:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Peer List Full");
      case ESP_ERR_ESPNOW_NOT_FOUND:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Peer Not Found");
      case ESP_ERR_ESPNOW_INTERNAL:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Internal");
      case ESP_ERR_ESPNOW_EXIST:
        mp_raise_msg(&mp_type_OSError, "ESP-Now Peer Exists");
      default:
        nlr_raise(mp_obj_new_exception_msg_varg(
          &mp_type_RuntimeError, "ESP-Now Unknown Error 0x%04x", e
        ));
   }
}

static inline void esp_espnow_exceptions(esp_err_t e) {
    if (e != ESP_OK) _esp_espnow_exceptions(e);
}

#define ESPNOW_EXCEPTIONS(x) do { esp_espnow_exceptions(x); } while (0);

static inline void _get_bytes(mp_obj_t str, size_t len, uint8_t *dst) {
    size_t str_len;
    const char *data = mp_obj_str_get_data(str, &str_len);  
    if (str_len != len) mp_raise_ValueError("bad len");
    memcpy(dst, data, len);
}

static mp_obj_t send_cb_obj = mp_const_none;
static mp_obj_t recv_cb_obj = mp_const_none;

STATIC void IRAM_ATTR send_cb(const uint8_t *macaddr, esp_now_send_status_t status)
{
    if (send_cb_obj != mp_const_none) {
        mp_obj_tuple_t *msg = mp_obj_new_tuple(2, NULL);
        msg->items[0] = mp_obj_new_bytes(macaddr, ESP_NOW_ETH_ALEN);
        msg->items[1] = (status == ESP_NOW_SEND_SUCCESS) ? mp_const_true : mp_const_false;
        mp_sched_schedule(send_cb_obj, msg);
        //printf("send_cb");
        //((void (*)(mp_obj_tuple_t*))send_cb_obj)(msg);
    }
}

#define BUFFERNUM (32)

typedef struct _my_obj_tuple_t {
    mp_obj_base_t base;
    size_t len;
    mp_obj_t items[2];
} my_obj_tuple_t;

static my_obj_tuple_t msg[BUFFERNUM];
static mp_obj_str_t item[BUFFERNUM][2];
static uint8_t my_g_i = 0;
byte recv_buffer[2][BUFFERNUM][256];

STATIC void IRAM_ATTR recv_cb(const uint8_t *macaddr, const uint8_t *data, int len) 
{
    if (recv_cb_obj != mp_const_none) {
#if 0
        mp_obj_tuple_t *msg = mp_obj_new_tuple(2, NULL);
        msg->items[0] = mp_obj_new_bytes(macaddr, ESP_NOW_ETH_ALEN);
        msg->items[1] = mp_obj_new_bytes(data, len);
        mp_sched_schedule(recv_cb_obj, msg);
#endif

        item[my_g_i][0].base.type=&mp_type_bytes;
        item[my_g_i][0].len = ESP_NOW_ETH_ALEN;
        item[my_g_i][0].hash = qstr_compute_hash(macaddr, ESP_NOW_ETH_ALEN);
        memcpy(recv_buffer[0][my_g_i], (byte*)macaddr, ESP_NOW_ETH_ALEN * sizeof(byte));

        item[my_g_i][1].base.type=&mp_type_bytes;
        item[my_g_i][1].len = len;
        item[my_g_i][1].hash = qstr_compute_hash(data, len);
        memcpy(recv_buffer[1][my_g_i], (byte*)data, len * sizeof(byte));

        msg[my_g_i].base.type = &mp_type_tuple;
        msg[my_g_i].len=2;
        msg[my_g_i].items[0] = MP_OBJ_FROM_PTR(&item[my_g_i][0]);
        msg[my_g_i].items[1] = MP_OBJ_FROM_PTR(&item[my_g_i][1]);

        mp_sched_schedule(recv_cb_obj, MP_OBJ_FROM_PTR(&msg[my_g_i]));

        if (++my_g_i > BUFFERNUM-1)
            my_g_i = 0;
    }
} 

static int initialized = 0;

STATIC mp_obj_t espnow_init() {
    if (!initialized) {
        ESPNOW_EXCEPTIONS(esp_now_init());
        initialized = 1;

        ESPNOW_EXCEPTIONS(esp_now_register_recv_cb(recv_cb));
        ESPNOW_EXCEPTIONS(esp_now_register_send_cb(send_cb));

	int i;
	for (i = 0;i < BUFFERNUM;i++) {
		item[i][0].data = recv_buffer[0][i];
		item[i][1].data = recv_buffer[1][i];
	}

    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(espnow_init_obj, espnow_init);

STATIC mp_obj_t espnow_deinit() {
    if (initialized) {
        ESPNOW_EXCEPTIONS(esp_now_deinit());
        initialized = 0;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(espnow_deinit_obj, espnow_deinit);

STATIC mp_obj_t espnow_on_send(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return send_cb_obj;
    }

    send_cb_obj = args[0];
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_on_send_obj, 0, 1, espnow_on_send);

STATIC mp_obj_t espnow_on_recv(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return recv_cb_obj;
    }

    recv_cb_obj = args[0];
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_on_recv_obj, 0, 1, espnow_on_recv);

// pmk(primary_key)
STATIC mp_obj_t espnow_pmk(mp_obj_t key) {
    uint8_t buf[ESP_NOW_KEY_LEN];
    _get_bytes(key, ESP_NOW_KEY_LEN, buf);
    ESPNOW_EXCEPTIONS(esp_now_set_pmk(buf));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_pmk_obj, espnow_pmk);

// lmk(peer_mac, local_key)
STATIC mp_obj_t espnow_lmk(mp_obj_t addr, mp_obj_t key) {
    mp_uint_t addr_len;
    const uint8_t *addr_buf = (const uint8_t *)mp_obj_str_get_data(addr, &addr_len);
    if (addr_len != ESP_NOW_ETH_ALEN) mp_raise_ValueError("addr invalid");
    esp_now_peer_info_t peer;
    ESPNOW_EXCEPTIONS(esp_now_get_peer(addr_buf, &peer));

    // set peer lmk
    bool encrypt = (key != mp_const_none);
    bool re_add = (peer.encrypt != encrypt);
    if (encrypt) _get_bytes(key, ESP_NOW_KEY_LEN, peer.lmk);
    if (re_add) {
        // workaround for calling esp_now_mod_peer() to 
        // change encryption status will crash the system
        peer.encrypt = encrypt;
        ESPNOW_EXCEPTIONS(esp_now_del_peer(addr_buf));
        ESPNOW_EXCEPTIONS(esp_now_add_peer(&peer));
    } else {
        ESPNOW_EXCEPTIONS(esp_now_mod_peer(&peer));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(espnow_lmk_obj, espnow_lmk);

// add_peer(peer_mac, [local_key])
STATIC mp_obj_t espnow_add_peer(size_t n_args, const mp_obj_t *args) {
    esp_now_peer_info_t peer = {0};
    _get_bytes(args[0], ESP_NOW_ETH_ALEN, peer.peer_addr);
    if (n_args > 1) {
        _get_bytes(args[1], ESP_NOW_KEY_LEN, peer.lmk);
        peer.encrypt = 1;
    }

    ESPNOW_EXCEPTIONS(esp_now_add_peer(&peer));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_add_peer_obj, 1, 2, espnow_add_peer);

// del_peer(peer_mac)
STATIC mp_obj_t espnow_del_peer(mp_obj_t addr) {
    mp_uint_t addr_len;
    const uint8_t *addr_buf = (const uint8_t *)mp_obj_str_get_data(addr, &addr_len);
    if (addr_len != ESP_NOW_ETH_ALEN) mp_raise_ValueError("addr invalid");
    ESPNOW_EXCEPTIONS(esp_now_del_peer(addr_buf));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_del_peer_obj, espnow_del_peer);

// this workaround enables ESP32 to send from whatever IF that is
// active
// if_id == wifi_mode - 1
#define IS_IF_AVAILABLE(mode, if_id) ({ (mode & (if_id+1)) != 0; })
#define AVAILABLE_IF(mode) ({                                       \
    int if_id = -1;                                                 \
    for (int i=WIFI_MODE_STA; i<=WIFI_MODE_AP; i++) {               \
        if (mode & i) {                                             \
            if_id = i-1;                                            \
        }                                                           \
    }                                                               \
    if_id;                                                          \
})                                                                  \

STATIC mp_obj_t espnow_send(mp_obj_t addr, mp_obj_t msg) {
    if (!wifi_started) goto espnow_wifi_err;

    mp_uint_t addr_len;
    const uint8_t *addr_buf;
    mp_uint_t msg_len;
    const uint8_t *msg_buf = (const uint8_t *)mp_obj_str_get_data(msg, &msg_len);
    if (msg_len > ESP_NOW_MAX_DATA_LEN) mp_raise_ValueError("msg too long");

    wifi_mode_t mode;
    ESPNOW_EXCEPTIONS(esp_wifi_get_mode(&mode));
    bool first = true;
    int new_if = -1;
    if (addr == mp_const_none) {
        // send to all
        esp_now_peer_info_t peer;
        esp_err_t e = esp_now_fetch_peer(true, &peer);
        ESPNOW_EXCEPTIONS(e); // raise error if nobody to send to
        while (e == ESP_OK) {
            if (!IS_IF_AVAILABLE(mode, peer.ifidx)) {
                if (first) {
                    new_if = AVAILABLE_IF(mode);
                    if (new_if < 0) goto espnow_wifi_err;
                    first = false;
                }
                peer.ifidx = new_if;
                ESPNOW_EXCEPTIONS(esp_now_mod_peer(&peer));
            }
            addr_buf = peer.peer_addr;
            ESPNOW_EXCEPTIONS(esp_now_send(addr_buf, msg_buf, msg_len));
            e = esp_now_fetch_peer(false, &peer);
        }
    } else {
        // send to one
        addr_buf = (const uint8_t *)mp_obj_str_get_data(addr, &addr_len);
        if (addr_len != ESP_NOW_ETH_ALEN) mp_raise_ValueError("addr invalid");

        esp_now_peer_info_t peer;
        ESPNOW_EXCEPTIONS(esp_now_get_peer(addr_buf, &peer));
        if (!IS_IF_AVAILABLE(mode, peer.ifidx)) {
            new_if = AVAILABLE_IF(mode);
            if (new_if < 0) goto espnow_wifi_err;
            peer.ifidx = new_if;
            ESPNOW_EXCEPTIONS(esp_now_mod_peer(&peer));
        }
        ESPNOW_EXCEPTIONS(esp_now_send(addr_buf, msg_buf, msg_len));
    }

    return mp_const_none;

espnow_wifi_err:
    mp_raise_msg(&mp_type_OSError, "wifi not active");
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(espnow_send_obj, espnow_send);

STATIC mp_obj_t espnow_peer_count() {
    esp_now_peer_num_t peer_num = {0};
    ESPNOW_EXCEPTIONS(esp_now_get_peer_num(&peer_num));

    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(peer_num.total_num);
    tuple[1] = mp_obj_new_int(peer_num.encrypt_num);
    return mp_obj_new_tuple(2, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(espnow_peer_count_obj, espnow_peer_count);

STATIC mp_obj_t espnow_version() {
    uint32_t version;
    ESPNOW_EXCEPTIONS(esp_now_get_version(&version));
    return mp_obj_new_int(version);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(espnow_version_obj, espnow_version);

STATIC const mp_rom_map_elem_t espnow_globals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_espnow) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&espnow_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&espnow_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_pmk), MP_ROM_PTR(&espnow_pmk_obj) },
    { MP_ROM_QSTR(MP_QSTR_lmk), MP_ROM_PTR(&espnow_lmk_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_peer), MP_ROM_PTR(&espnow_add_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_del_peer), MP_ROM_PTR(&espnow_del_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&espnow_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_send), MP_ROM_PTR(&espnow_on_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_recv), MP_ROM_PTR(&espnow_on_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_count), MP_ROM_PTR(&espnow_peer_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&espnow_version_obj) },
};
STATIC MP_DEFINE_CONST_DICT(espnow_globals_dict, espnow_globals_dict_table);

const mp_obj_module_t mp_module_esp_espnow = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&espnow_globals_dict,
};

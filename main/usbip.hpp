#pragma once
#include "byteswap.h"
#include <string.h>

// commands
#define OP_REQ_DEVLIST 0x8005
#define OP_REP_DEVLIST 0x0005
#define OP_REQ_IMPORT 0x8003
#define OP_REP_IMPORT 0x0003

// usbip_header_basic commands
#define USBIP_CMD_SUBMIT 0x0001
#define USBIP_RET_SUBMIT 0x0003
#define USBIP_CMD_UNLINK 0x0002
#define USBIP_RET_UNLINK 0x0004

#define USBIP_VERSION 0x1101

typedef struct{
    uint16_t version;
    uint16_t command;
    uint32_t status;
}usbip_request_t;

typedef struct{
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t padding;
}usbip_interface_t;

typedef struct{
    usbip_request_t request;
    uint32_t count;
    char path[256];
    char busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
    usbip_interface_t intfs[10];
}usbip_devlist_t;

typedef struct{
    usbip_request_t request;
    char path[256];
    char busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
}usbip_import_t;

typedef struct{
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
}usbip_header_basic_t;

typedef struct{
    usbip_header_basic_t header;
    union{
        uint32_t flags;
        uint32_t status;
    };
    uint32_t length;
    uint32_t start_frame;
    uint32_t num_packets;
    union{
        uint32_t interval;
        uint32_t error_count;
    };
    union{
        uint64_t setup;
        uint64_t padding;
    };
    uint8_t transfer_buffer[1024];
}__attribute__((__packed__))usbip_submit_t;

typedef struct{
    usbip_header_basic_t header;
    union{
        uint32_t unlink_seqnum;
        uint32_t status;
    };
    uint8_t padding[24] = {};
}usbip_unlink_t;

typedef struct{
    int len;
    uint8_t* rx_buffer;
}urb_data_t;

#include "usb/usb_host.h"
#include "esp_event.h"
#include "usb_device.hpp"

ESP_EVENT_DECLARE_BASE( USBIP_EVENT_BASE );


extern usb_device_info_t info;
extern const usb_device_desc_t *dev_desc;


class USBipDevice : public USBhostDevice
{
private:
    // friend void usb_ctrl_cb(usb_transfer_t *transfer);
    const usb_ep_desc_t * ep_out;
    const usb_ep_desc_t * endpoints[15][2];
    const usb_config_desc_t *config_desc;

public:
    USBipDevice();
    ~USBipDevice();
    bool init(USBhost*);

    void fill_import_data(usbip_import_t* resp);
    void fill_list_data(usbip_devlist_t* resp);
    int req_ctrl_xfer(usbip_submit_t* req);
    int req_ep_xfer(usbip_submit_t* req);
};

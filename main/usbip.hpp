#pragma once
#include <string.h>
#include "usb/usb_host.h"
#include "esp_event.h"
#include "usb_device.hpp"

/* Swap bytes in 16-bit value.  */
#define bswap_constant_16(x)					\
  ((__uint16_t) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))

/* Swap bytes in 32-bit value.  */
#define bswap_constant_32(x)					\
  ((((x) & 0xff000000u) >> 24) | (((x) & 0x00ff0000u) >> 8)	\
   | (((x) & 0x0000ff00u) << 8) | (((x) & 0x000000ffu) << 24))


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
    uint32_t command;   /*!< command */
    uint32_t seqnum;    /*!< seqnum: sequential number that identifies requests and corresponding responses; incremented per connection */
    uint32_t devid;     /*!< devid: specifies a remote USB device uniquely instead of busnum and devnum; for client (request), this value is ((busnum << 16) | devnum); for server (response), this shall be set to 0 */
    uint32_t direction; /*!< direction: only used by client, for server this shall be 0 */
    uint32_t ep;        /*!< ep: endpoint number only used by client, for server this shall be 0; for UNLINK, this shall be 0 */
}usbip_header_basic_t;

typedef struct{
    usbip_header_basic_t header;
    union{
        uint32_t flags;             /*!< cmd => transfer_flags: possible values depend on the URB transfer_flags (refer to URB doc in USB Request Block (URB)) but with URB_NO_TRANSFER_DMA_MAP masked. */
        uint32_t status;            /*!< resp => status: zero for successful URB transaction, otherwise some kind of error happened. */
    };
    uint32_t length;                /*!< transfer_buffer_length: use URB transfer_buffer_length */
    uint32_t start_frame;           /*!< start_frame: use URB start_frame; initial frame for ISO transfer; shall be set to 0 if not ISO transfer */
    uint32_t num_packets;           /*!< number_of_packets: number of ISO packets; shall be set to 0xffffffff if not ISO transfer */
    union{
        uint32_t interval;          /*!< cmd => interval: maximum time for the request on the server-side host controller */
        uint32_t error_count;       /*!< resp => error_count */
    };
    union{
        uint64_t setup;             /*!< cmd => setup: data bytes for USB setup, filled with zeros if not used. */
        uint64_t padding;           /*!< resp => padding, shall be set to 0 */
    };
    /**
     * @brief 
     * transfer_buffer. If direction is USBIP_DIR_OUT then n equals transfer_buffer_length; otherwise n equals 0. For ISO transfers the padding between each ISO packets is not transmitted.
     * transfer_buffer. If direction is USBIP_DIR_IN then n equals actual_length; otherwise n equals 0. For ISO transfers the padding between each ISO packets is not transmitted.
     */
    uint8_t transfer_buffer[1024];
}__attribute__((__packed__))usbip_submit_t;

typedef struct{
    usbip_header_basic_t header;
    union{
        int32_t unlink_seqnum;
        int32_t status;
    };
    uint8_t padding[24] = {};
}usbip_unlink_t;

typedef struct{
    int socket;
    int len;
    uint8_t* rx_buffer;
}urb_data_t;

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

    int req_ctrl_xfer(usbip_submit_t* req);
    int req_ep_xfer(usbip_submit_t* req);

private:
    void fill_import_data();
    void fill_list_data();
};

class USBIP
{
private:
    
public:
    USBIP();
    ~USBIP();
};

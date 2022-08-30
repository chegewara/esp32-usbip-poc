#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "byteswap.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "usbip.hpp"

// commands
#define OP_REQ_DEVLIST bswap_constant_16(0x8005)
#define OP_REP_DEVLIST bswap_constant_16(0x0005)
#define OP_REQ_IMPORT bswap_constant_16(0x8003)
#define OP_REP_IMPORT bswap_constant_16(0x0003)

// usbip_header_basic commands
#define USBIP_CMD_SUBMIT bswap_constant_16(0x01)
#define USBIP_RET_SUBMIT bswap_constant_32(0x03)
#define USBIP_CMD_UNLINK bswap_constant_16(0x02)
#define USBIP_RET_UNLINK bswap_constant_32(0x04)

#define USBIP_VERSION   bswap_constant_16(0x0111)   // v1.11
#define USB_LOW_SPEED   bswap_constant_32(1)
#define USB_FULL_SPEED  bswap_constant_32(2)

static usbip_import_t import_data = {};
static usbip_devlist_t devlist_data = {};
static uint32_t last_seqnum = 0;
static uint32_t last_unlink = 0;

usb_device_info_t info;
const usb_device_desc_t *dev_desc;
static esp_event_loop_handle_t loop_handle;
static SemaphoreHandle_t usb_sem;
static SemaphoreHandle_t usb_sem1;
static int _sock;

static bool is_ready = false;
static bool finished = false;
static usb_transfer_t *_transfer;

#define USB_CTRL_RESP   0x1001
#define USB_EPx_RESP    0x1002

ESP_EVENT_DECLARE_BASE( USBIP_EVENT_BASE );
ESP_EVENT_DEFINE_BASE(USBIP_EVENT_BASE);

#define TAG "usbip"
#include <algorithm>
#include <vector>
static std::vector<uint32_t> vec;

static void usb_ctrl_cb(usb_transfer_t *transfer)
{
    esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USB_CTRL_RESP, (void*)&transfer, sizeof(usb_transfer_t*), 10);
}

static void usb_read_cb(usb_transfer_t *transfer)
{
    esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USB_EPx_RESP, (void*)&transfer, sizeof(usb_transfer_t*), 10);
}

static void _event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id)
    {
    case USB_CTRL_RESP:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usb_transfer_t *transfer = *(usb_transfer_t **)event_data;
        usbip_submit_t* req = (usbip_submit_t*)transfer->context;
        uint32_t seqnum = __bswap_32(req->header.seqnum);
        if (std::find(vec.begin(), vec.end(), seqnum) != vec.end())
        {
            delete req;
            dev->deallocate(transfer);
            break;
        }
        vec.insert(vec.begin(), seqnum);
        if(vec.size() >= 999) vec.pop_back();

        int _len = transfer->actual_num_bytes - 8; //__bswap_32(req->length);
        if(_len < 0) {
            dev->deallocate(transfer);   
            break;
        }
        if (req->header.direction == 0) // 0: USBIP_DIR_OUT
        {
            _len = 0;
        }

        req->header.command = USBIP_RET_SUBMIT;
        req->header.devid = 0;
        req->header.direction = 0;
        req->header.ep = 0;
        req->status = 0;
        // TODO: transfer_buffer. If direction is USBIP_DIR_IN then n equals actual_length; 
        // otherwise n equals 0. 
        // For ISO transfers the padding between each ISO packets is not transmitted.
        req->length = __bswap_32(_len);
        req->padding = 0;
        if(_len) memcpy(&req->transfer_buffer[0], transfer->data_buffer + 8, _len);
        // req->transfer_buffer = transfer->data_buffer + 8;
        if(transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            _len = 0;
            req->length = 0;
            req->status = -ETIME;
            req->error_count = 1;
        }
        int to_write = 0x30 + _len;
        ESP_LOG_BUFFER_HEX_LEVEL("USB_CTRL_RESP", (void*)req, to_write, ESP_LOG_WARN);
        send(_sock, (void*)req, to_write, MSG_DONTWAIT);
        delete req;
        dev->deallocate(transfer);
        break;
    }

    case USB_EPx_RESP:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usb_transfer_t *transfer = *(usb_transfer_t **)event_data;
        usbip_submit_t* req = (usbip_submit_t*)transfer->context;
        uint32_t seqnum = __bswap_32(req->header.seqnum);
        if (std::find(vec.begin(), vec.end(), seqnum) != vec.end())
        {
            delete req;
            dev->deallocate(transfer);
            break;
        }
        vec.insert(vec.begin(), seqnum);
        if(vec.size() >= 999) vec.pop_back();

        int _len = transfer->actual_num_bytes;
        if(_len <= 0) {
            dev->deallocate(transfer);   
            break;
        }
        if (req->header.direction == 0)
        {
            _len = 0;
        }

        req->header.command = USBIP_RET_SUBMIT;
        req->header.devid = 0;
        req->header.direction = 0;
        req->header.ep = 0;
        req->status = 0;
        // TODO: transfer_buffer. If direction is USBIP_DIR_IN then n equals actual_length; 
        // otherwise n equals 0. 
        // For ISO transfers the padding between each ISO packets is not transmitted.
        req->length = __bswap_32(_len);
        req->start_frame = 0;
        req->padding = 0;
        memcpy(&req->transfer_buffer[0], transfer->data_buffer, transfer->actual_num_bytes);
        if(transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            _len = 0;
            req->length = 0;
            req->status = -ETIME;
            req->error_count = 1;
        }
        int to_write = 0x30 + _len;
        ESP_LOG_BUFFER_HEX_LEVEL("USB_EPx_RESP", (void*)req, to_write, ESP_LOG_WARN);

        send(_sock, (void*)req, to_write, MSG_DONTWAIT);
        delete req;
        dev->deallocate(transfer);
        break;
    }

    default:
        break;
    }
}

static void _event_handler1(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id)
    {
    case USBIP_CMD_SUBMIT:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        urb_data_t* data = (urb_data_t*)event_data;
        int socket = data->socket;
        uint8_t* rx_buffer = (uint8_t*) data->rx_buffer;
        int len = data->len;
        int start = 0;
        int _len = len;
        usbip_submit_t* __req = (usbip_submit_t*)(rx_buffer);
        uint32_t seqnum = __bswap_32(__req->header.seqnum);

        do
        {
            ESP_LOGW(TAG, "USBIP_CMD_SUBMIT: start 0x%02x, len: %d", start, _len);
            ESP_LOG_BUFFER_HEX("SUBMIT", rx_buffer + start, 48);

            usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer + start);
            usbip_submit_t* req = new usbip_submit_t();
            int tl = 0;
            if(_req->header.direction == 0) tl = __bswap_32(_req->length);

            memcpy(req, _req, 0x30 + tl);
            
            ESP_LOGW(TAG, "request ep: %d", __bswap_32(req->header.ep));
            int tlen = 0;

            if(req->header.ep == 0) // EP0
            {
                tlen = dev->req_ctrl_xfer(req);
                if(tlen > 0){
                    ESP_LOG_BUFFER_HEX_LEVEL("SUBMIT 7", rx_buffer + start, 48 + tlen, ESP_LOG_ERROR);
                }
                tlen = 0;
            } else { // EPx
                tlen = dev->req_ep_xfer(req);
                if(tlen > 0){
                    ESP_LOG_BUFFER_HEX_LEVEL("SUBMIT 10", rx_buffer + start, 48 + tlen, ESP_LOG_ERROR);
                }
            }
            start += 0x30 + tlen;
            _len -= 0x30 + tlen;
            ESP_LOGI(TAG, "USBIP_CMD_SUBMIT: end 0x%02x, len: %d", start, _len);
        } 
        while (_len >= 0x30);
        break;
    }

    case USBIP_CMD_UNLINK:{
        usbip_unlink_t* req = *(usbip_unlink_t**)event_data;
        req->header.command = USBIP_RET_UNLINK;
        req->header.devid = 0;
        req->header.direction = 0;
        req->header.ep = 0;
        req->status = 0;
        int to_write = 48;
        send(_sock, (void*)req, to_write, MSG_DONTWAIT);
        ESP_LOG_BUFFER_HEX(TAG, (void*)req, 48);
        delete req;
        break;
    }

    default:
        break;
    }
}

static void _event_handler2(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch(event_id)
    {
        case OP_REQ_DEVLIST:{
            int to_write = 0;
            // 0xC + i*0x138 + m_(i-1)*4
            if(devlist_data.request.version == 0) /*!< assume there is no device connected yet */
            {
                to_write = 12;
                devlist_data.request.version = USBIP_VERSION;
                devlist_data.request.command = OP_REP_DEVLIST;
                devlist_data.request.status = 0;
                devlist_data.count = 0;
            } else {
                to_write = 0x0c + __bswap_32(devlist_data.count) * 0x138 + devlist_data.bNumInterfaces * 4;
            }
            send(_sock, (void*)&devlist_data, to_write, MSG_DONTWAIT);
            break;
        }

        case OP_REQ_IMPORT:{
            int to_write = sizeof(usbip_import_t);
            send(_sock, (void*)&import_data, to_write, MSG_DONTWAIT);
            break;
        }
    }
}

USBipDevice::USBipDevice()
{
    usb_sem = xSemaphoreCreateBinary();
    usb_sem1 = xSemaphoreCreateBinary();
    xSemaphoreGive(usb_sem);
    xSemaphoreGive(usb_sem1);

    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USB_CTRL_RESP, _event_handler, this);
    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USB_EPx_RESP, _event_handler, this);
    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_SUBMIT, _event_handler1, this);
    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_UNLINK, _event_handler1, this);
}

USBipDevice::~USBipDevice()
{
    esp_event_handler_unregister_with(loop_handle, USBIP_EVENT_BASE, ESP_EVENT_ANY_ID, _event_handler);
    esp_event_handler_unregister_with(loop_handle, USBIP_EVENT_BASE, ESP_EVENT_ANY_ID, _event_handler1);
    memset(&import_data, 0, sizeof(usbip_import_t));
    memset(&devlist_data, 0, sizeof(usbip_devlist_t));
}

bool USBipDevice::init(USBhost* host)
{
    _host = host;

    usb_device_info_t info = host->getDeviceInfo();
    USBhostDevice::init(1032);
    xfer_ctrl->callback = usb_ctrl_cb;

    config_desc = host->getConfigurationDescriptor();
    
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        const usb_ep_desc_t *ep = nullptr;

        for (size_t i = 0; i < intf->bNumEndpoints; i++)
        {
            int _offset = 0;
            ep = usb_parse_endpoint_descriptor_by_index(intf, i, config_desc->wTotalLength, &_offset);
            uint8_t adr = ep->bEndpointAddress;
            if (adr & 0x80)
            {
                endpoints[adr & 0xf][1] = ep;
            } else {
                endpoints[adr & 0xf][0] = ep;
            }

            printf("EP num: %d/%d, len: %d, ", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
            if (ep)
                printf("address: 0x%02x, EP max size: %d, dir: %s\n", ep->bEndpointAddress, ep->wMaxPacketSize, (ep->bEndpointAddress & 0x80) ? "IN" : "OUT");
            else
                ESP_LOGW("", "error to parse endpoint by index; EP num: %d/%d, len: %d", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
        }
        esp_err_t err = usb_host_interface_claim(_host->clientHandle(), _host->deviceHandle(), n, 0);
        ESP_LOGI("", "interface claim status: %d", err);
    }

    fill_list_data();
    fill_import_data();
    return true;
}

void USBipDevice::fill_import_data()
{
    memset(&import_data, 0, sizeof(usbip_import_t));
    import_data.request.version = USBIP_VERSION;
    import_data.request.command = OP_REP_IMPORT;
    import_data.request.status = 0;
    strcpy(import_data.path, "/espressif/usbip/usb1");
    strcpy(import_data.busid, "1-1");
    import_data.busnum = __bswap_32(1);
    import_data.devnum = __bswap_32(1);

    import_data.speed = info.speed ? __bswap_32(2) : __bswap_32(1);
    devlist_data.idVendor = __bswap_16(dev_desc->idVendor);
    devlist_data.idProduct = __bswap_16(dev_desc->idProduct);
    devlist_data.bcdDevice = __bswap_16(dev_desc->bcdDevice);
    import_data.bDeviceClass = dev_desc->bDeviceClass;
    import_data.bDeviceSubClass = dev_desc->bDeviceSubClass;
    import_data.bDeviceProtocol = dev_desc->bDeviceProtocol;
    import_data.bConfigurationValue = config_desc->bConfigurationValue;
    import_data.bNumConfigurations = dev_desc->bNumConfigurations;
    import_data.bNumInterfaces = config_desc->bNumInterfaces;
}

void USBipDevice::fill_list_data()
{
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        devlist_data.intfs[n].bInterfaceClass = intf->bInterfaceClass,
        devlist_data.intfs[n].bInterfaceSubClass = intf->bInterfaceSubClass,
        devlist_data.intfs[n].bInterfaceProtocol = intf->bInterfaceProtocol,
        devlist_data.intfs[n].padding  = 0;
    }

    devlist_data.request.version = USBIP_VERSION;
    devlist_data.request.command = OP_REP_DEVLIST;
    devlist_data.request.status = 0;
    devlist_data.busnum = __bswap_32(1);
    devlist_data.devnum = __bswap_32(1);
    devlist_data.count = __bswap_32(1);
    strcpy(devlist_data.path, "/espressif/usbip/usb1");
    strcpy(devlist_data.busid, "1-1");

    devlist_data.speed = info.speed ? USB_FULL_SPEED : USB_LOW_SPEED;
    devlist_data.idVendor = __bswap_16(dev_desc->idVendor);
    devlist_data.idProduct = __bswap_16(dev_desc->idProduct);
    devlist_data.bcdDevice = __bswap_16(dev_desc->bcdDevice);
    devlist_data.bDeviceClass = dev_desc->bDeviceClass;
    devlist_data.bDeviceSubClass = dev_desc->bDeviceSubClass;
    devlist_data.bDeviceProtocol = dev_desc->bDeviceProtocol;
    devlist_data.bConfigurationValue = config_desc->bConfigurationValue;
    devlist_data.bNumConfigurations = dev_desc->bNumConfigurations;
    devlist_data.bNumInterfaces = config_desc->bNumInterfaces;
}

int USBipDevice::req_ctrl_xfer(usbip_submit_t* req)
{
    usb_transfer_t* _xfer_ctrl = allocate(1000);
    _xfer_ctrl->callback = usb_ctrl_cb;
    _xfer_ctrl->context = req;
    _xfer_ctrl->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);

    usb_setup_packet_t * temp = (usb_setup_packet_t *)_xfer_ctrl->data_buffer;
    size_t n = 0;
    if (req->header.direction == 0) // 0: USBIP_DIR_OUT
    {
        n = __bswap_32(req->length);
        memcpy(_xfer_ctrl->data_buffer, (void*)&req->transfer_buffer, n);
    }
    int _n = __bswap_32(req->length);
    memcpy(temp->val, (uint8_t*)&req->setup, 8 + n);
    // TODO: transfer_buffer. If direction is USBIP_DIR_OUT then n equals transfer_buffer_length; 
    // otherwise n equals 0. 
    // For ISO transfers the padding between each ISO packets is not transmitted.
    _xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + __bswap_32(req->length);
    _xfer_ctrl->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);
    _xfer_ctrl->context = req;
    
    esp_err_t err = usb_host_transfer_submit_control(_host->clientHandle(), _xfer_ctrl);

    return  n;
}

int USBipDevice::req_ep_xfer(usbip_submit_t* req)
{
    size_t _len = __bswap_32(req->length);
    // ESP_LOG_BUFFER_HEX_LEVEL("xfer ep", req, 48, ESP_LOG_ERROR);

    uint16_t mps = 64;

    if (req->header.direction != 0)
    {
        uint8_t adr = __bswap_32(req->header.ep);
        const usb_ep_desc_t *ep = endpoints[adr][1];
        if (ep)
        {
            mps = ep->wMaxPacketSize;
        } else {
            ESP_LOGE("", "missing EP%d\n", adr);
            return 0;
        }

        _len = usb_round_up_to_mps(_len, mps);
        // ESP_LOGE("", "mps: %d[%d]\n", mps, _len);
    }
    else
    {
        ESP_LOG_BUFFER_HEX("", req, _len);
    }

    usb_transfer_t *xfer_read = allocate(_len);
    if(xfer_read == NULL) return 0;
    xfer_read->callback = &usb_read_cb;
    xfer_read->context = req;
    xfer_read->bEndpointAddress = __bswap_32(req->header.ep);
    // printf("req_ep_xfer ep: %d[%d], dir: %d\n", __bswap_32(req->header.ep), xfer_read->bEndpointAddress, __bswap_32(req->header.direction));
    ESP_LOG_BUFFER_HEX_LEVEL("", req, 48, ESP_LOG_WARN);

    int n = 0;
    if (req->header.direction == 0)
    {
        memcpy(xfer_read->data_buffer, (void*)&req->transfer_buffer, _len);
        n = _len;
    }
    int _n = _len;

    // TODO: transfer_buffer. If direction is USBIP_DIR_OUT then n equals transfer_buffer_length; 
    // otherwise n equals 0. 
    // For ISO transfers the padding between each ISO packets is not transmitted.
    xfer_read->num_bytes = _len;
    // if(xfer_read->num_bytes == 0x100 && req->header.direction != 0) xfer_read->num_bytes = 0xff;
    xfer_read->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);
    // ESP_LOG_BUFFER_HEX("", temp->val, len - 40);
    // printf("num_bytes_epx[%d/%d]: %d\n", n, _n, xfer_read->num_bytes);
    xfer_read->context = req;

    esp_err_t err = usb_host_transfer_submit(xfer_read);

    return n;
}

// TODO: switch it to events
extern "C" void parse_request(const int sock, uint8_t* rx_buffer, size_t len)
{
    uint32_t cmd = ((usbip_request_t*)rx_buffer)->command;
    _sock = sock;

    switch (cmd)
    {
    case OP_REQ_DEVLIST:{
        ESP_LOGI(TAG, "OP_REQ_DEVLIST");
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, OP_REQ_DEVLIST, NULL, 0, 10);
        break;
    }
    case OP_REQ_IMPORT:{
        ESP_LOGI(TAG, "OP_REQ_IMPORT");
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, OP_REQ_IMPORT, NULL, 0, 10);
        break;
    }
    case USBIP_CMD_SUBMIT:{
        ESP_LOGI(TAG, "USBIP_CMD_SUBMIT");
        urb_data_t data = {
             .socket = sock,
             .len = (int)len,
             .rx_buffer = rx_buffer
        };
        usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer);
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_SUBMIT, &data, len + sizeof(int), 10);
        break;
    }
    case USBIP_CMD_UNLINK:{
        ESP_LOGI(TAG, "USBIP_CMD_UNLINK");
        usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer);
        usbip_submit_t* req = new usbip_submit_t(); // make it heap caps malloc
        last_unlink = __bswap_32(_req->flags);
        vec.insert(vec.begin(), last_unlink);
        memcpy(req, _req, 0x30);
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_UNLINK, &req, sizeof(usbip_submit_t*), 10);
        break;
    }
    default:
        ESP_LOGE(TAG, "unknown command: %" PRIu32, cmd); // PRIu32
        break;
    }
}


USBIP::USBIP()
{
    esp_event_loop_args_t loop_args = {
        .queue_size = 100,
        .task_name = "usbip_events",
        .task_priority = 21,
        .task_stack_size = 4*1024,
        .task_core_id = 0
    };

    esp_event_loop_create(&loop_args, &loop_handle);

    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, OP_REQ_DEVLIST, _event_handler2, NULL); /*!< handle list USB devices - `usbip list -r myIP` */
    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, OP_REQ_IMPORT, _event_handler2, NULL);
}

USBIP::~USBIP() {}

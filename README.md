# Description
This is proof of concept of USBIP in esp32 S2/S3 SoC.

## Start usbip driver
After installing `usbip` on linux, we need to modeprobe module.
`sudo modprobe vhci-hcd`

## Linux commands
Basic commands to list, attach and deatach devices:
- `usbip list -r 192.168.0.108`
- `usbip --tcp-port 3240 list -r 192.168.0.108`
- `sudo usbip attach --remote 192.168.0.108 -b 1-1`
- `sudo usbip detach -p 0`

- `sudo ln -s /var/lib/usbutils/usb.ids /usr/share/hwdata/usb.ids`

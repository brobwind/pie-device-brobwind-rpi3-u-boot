### The bootloader for Raspberry Pi 3 to boot Android 9 Pie

#### Setup build environment
Assuming you already downloaded Android 9 Pie source code and the device configuration files
from https://github.com/brobwind/pie-device-brobwind-rpi3.

And already built the whole project.

#### Download source code
1. Download the source code from https://github.com/brobwind/pie-device-brobwind-rpi3-u-boot/
```bash
$ git clone https://github.com/brobwind/pie-device-brobwind-rpi3-u-boot/ device/brobwind/rpi3/u-boot
```

2. Download mkknlimg from https://github.com/raspberrypi/linux/blob/rpi-4.14.y/scripts/mkknlimg
```bash
$ curl https://raw.githubusercontent.com/raspberrypi/linux/rpi-4.14.y/scripts/mkknlimg > device/brobwind/rpi3/u-boot/mkknlimg
$ chmod +x device/brobwind/rpi3/u-boot/mkknlimg
```

#### Build
Assuming current path is device/brobwind/rpi3/u-boot, execute following commands will build a 64-bit u-boot.
1. Run configuration
```bash
$ ARCH=arm64 CROSS_COMPILE=../../../../prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-androidkernel- make rpi_3_defconfig
```

2. Make
```bash
$ ARCH=arm64 CROSS_COMPILE=../../../../prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-androidkernel- make
```

3. Add a trailer
```bash
$ ./mkknlimg --dtok --ddtk --270x u-boot.bin u-boot-dtok.bin
```

#### Common usage
Uses a USB-to-TTL serial cable to connect the Raspberry Pi to PC and uses minicom to interactive with the bootloader.
https://developer.android.com/things/hardware/raspberrypi#serial-console

The serial port setup parameter is 115200,8n1:
```bash
$ sudo minicom -b 115200 -D /dev/ttyUSB0
```
Also connect to a wired Ethernet network and the IP address can obtain from DHCP

1. Print environment information
```bash
U-Boot> printenv
```
It will show information like this:
> android\_rootdev="/dev/mmcblk${android\_root\_devnum}p${android\_root\_partnum}"
>
> autoload=no
>
> board=rpi3
>
> board\_name=3 Model B+
>
> board\_rev=0xD
>
> board\_rev\_scheme=1
>
> board\_revision=0xA020D3
>
> bootcmd=\\
>
>   fdt addr "${fdt\_addr}"; \\
>
>   fdt resize "${oem\_overlay\_max\_size}"; \\
>
>   android\_ab\_select "android\_slot" mmc "0;misc" || run fastbootcmd || reset; \\
>
>   test "${android\_slot}" = "a" && env set oem\_part "${oem\_bootloader\_a}"; \\
>
>   test "${android\_slot}" = "b" && env set oem\_part "${oem\_bootloader\_b}"; \\
>
>   ext2load mmc "0:${oem\_part}" "${fdt\_high}" /kernel.dtbo && \\
>
>       fdt apply "${fdt_high}"; \\
>
>   fdt get value bootargs /chosen bootargs; \\
>
>   setenv bootargs "${bootargs} androidboot.serialno=${serial#}"; \\
>
>   boot\_android mmc "0;misc" "${android\_slot}" "${kernel\_addr\_r}"; \\
>
>   reset
>
> ethaddr=b8:27:eb:3a:df:1c
>
> fastbootcmd=\\
>
>   usb start && \\
>
>   dhcp && \\
>
>   fastboot udp
>
> fdt\_addr=2eff9400
>
> fdt\_high=2e000000

2. Run in fastboot mode
```bash
U-Boot> run fastbootcmd
```
It will show information:
> starting USB...
>
> USB0:   scanning bus 0 for devices... 5 USB Device(s) found
>
>        scanning usb for storage devices... 0 Storage Device(s) found
>
> lan78xx\_eth Waiting for PHY auto negotiation to complete..... done
>
> BOOTP broadcast 1
>
> DHCP client bound to address 192.168.5.211 (6 ms)
>
> lan78xx\_eth Waiting for PHY auto negotiation to complete...... done
>
> Using lan78xx\_eth device
>
> Listening for fastboot command on 192.168.5.211

3. Flash images

Once the bootloader run in fastboot mode, you can use fastboot command to download images:
```bash
$ fastboot -s udp:192.168.5.211 flash rpiboot out/target/product/rpi3/rpiboot.img
$ fastboot -s udp:192.168.5.211 flash boot_a out/target/product/rpi3/boot.img
$ fastboot -s udp:192.168.5.211 flash system_a out/target/product/rpi3/system.img
$ fastboot -s udp:192.168.5.211 flash vendor_a out/target/product/rpi3/vendor.img
```

4. Erase userdata partition
```bash
$ fastboot -s udp:192.168.5.211 erase userdata
```

5. Run boot command
```bash
U-Boot> run bootcmd
```

6. Show flat device tree
```bash
U-Boot> fdt addr ${fdt_addr}
U-Boot> fdt print
```
#### For detail info, please refer:
1. https://www.brobwind.com/archives/1600

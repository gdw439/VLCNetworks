#-------------------------------------------------------------------------------
# Name:        package_send.py
# Purpose:     发包测试测试组网系统的丢包和误包率
#
# Author:      vortex
#
# Created:     09/08/2020
# Copyright:   (c) vortex 2020
# Licence:     <your licence>
#-------------------------------------------------------------------------------
import zlib                 #  CRC32校验工具
from scapy.all import *     # 以太网发包工具
from random import randint  # 生成测试伪随机数据

class PackageSend:
    ''' 发包测试测试组网系统的丢包和误包率 '''

    def __init__(self, pkgs=1e8, lens=1024, iface='ens33'):
        ''' 初始化发送数据参数
            pkgs : 测试数据包的数目
            lens : 每个包的长度
            ifcae: 网口名称
        '''
        self.pkgs  = int(pkgs)
        self.lens  = int(lens)
        self.iface = iface


    def pkg_send(self, dst='AA:AA:AA:AA:AA:AA', src='DD:DD:DD:DD:DD:DD', type='0xfeed'):
        ''' 配置好参数之后， 该函数完成发包过程
            dst  : 目的MAC
            src  : 源MAC
            type : 以太网类型
        '''
        for item in range(self.pkgs):
            # 随机生成包含包头编号的 payload  --- ID<4> | payload<lens> | CRC32<4> ---

            data =item.to_bytes(length=4, byteorder='big', signed=False)
            for iter in range(self.lens - 4 - 4):
                data = data + randint(0, 255).to_bytes(length=1, byteorder='big', signed=False)

            # 计算CRC校验
            pkg_crc = zlib.crc32(data).to_bytes(length=4, byteorder='big', signed=False)

            # 校验打包
            data = data + pkg_crc

            # 打包以太网帧（可能外面还会包一层CRC32)
            pkg = Ether(dst=dst, src=src) / data
            pkg.type = eval(type)

            # 网口发送数据
            sendp(pkg, iface=self.iface)

            if item % 100 == 0:
                print("Finish Package:", item)


    def pkg_wait(self):
        ''' 等待UE提供发包信号
        '''
        print("waiting for test begining...")

        # 当收到该类型的以太网包时开始发送测试帧
        sniff(filter='ether[12:2] = 0xfeed',iface=self.iface, count=1)

        print("test begins...")



if __name__ == '__main__':

    #### iface 修改为本电脑的网口名称
    a = PackageSend(iface='Intel(R) 82579LM Gigabit Network Connection')

    # 等待发包指令
    a.pkg_wait()

    print("Start package send...")

    # 循环执行发包
    while True:
        #### 修改dst为UE端的MAC地址
        a.pkg_send(dst='AA:AA:AA:AA:AA:AA')

    print("package over.")

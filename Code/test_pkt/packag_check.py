#-------------------------------------------------------------------------------
# Name:        package_check.py
# Purpose:     发包测试测试组网系统的丢包和误包率
#
# Author:      vortex
#
# Created:     10/08/2020
# Copyright:   (c) vortex 2020
# Licence:     <your licence>
#-------------------------------------------------------------------------------

import zlib                 #  CRC32校验工具
from scapy.all import *     # 以太网发包工具

class PackageCapture:
    ''' 抓包测试丢包程序，该程序在UE端电脑执行 '''

    def __init__(self, iface='ens33'):
        ''' ifcae: 所探测的网卡名称
        '''
        self.counter  = 0
        self.pkg_error = 0
        self.pkg_discard = 0
        self.iface    = iface
        self.filter   = 'ether[12:2] = 0xfeed'


    def pkg_check(self, packet):
        ''' 对过滤后的数据包进行CRC校验，统计丢包和误包率
        '''
        payload = raw(packet['Ether'].payload)
        pkg_id = int.from_bytes(payload[0:4], 'big')

        # 计算校验是否正常
        pkg_crc = zlib.crc32(payload).to_bytes(length=4, byteorder='big', signed=False)

        # 如果校验正确
        if pkg_crc == 0:
            if pkg_id == 0:
                self.show_status()

            # 若ID正常，那么更新本地ID
            if pkg_id == self.counter:
                self.counter += 1
            # 若ID错误，统计丢包
            else:
                self.counter = pkg_id
                self.pkg_discard += pkg_id - self.counter

        # 若校验出错，该包出错，但仍统计收包
        else:
            self.pkg_error += 1
            self.counter += 1


    def pkg_filter(self, filter):
        ''' 设置过滤条件
            过滤条件规则参考： http://alumni.cs.ucr.edu/~marios/ethereal-tcpdump.pdf
        '''
        self.filter = filter


    def eth_filter(self):
        ''' 根据以太网帧头过滤
        '''
        filter =                     'ether[6:2] = 0xDDDD'
        filter = filter + ' and ' +  'ether[8:2] = 0xDDDD'
        filter = filter + ' and ' + 'ether[10:2] = 0xDDDD'
        filter = filter + ' and ' + 'ether[12:2] = 0xfeed'
        self.filter = filter


    def show_status(self):
        print("total package\t:", self.counter)
        print("current error package\t:", self.pkg_error)
        print("current discard package\t:", self.pkg_discard)

        self.counter = 0
        self.pkg_error = 0
        self.pkg_discard = 0


    def package_cap(self):
        '''开始抓包
        '''
        sniff(filter=self.filter,iface=self.iface, prn=self.pkg_check)


    def req_test(self, dst='DD:DD:DD:DD:DD:DD', src='AA:AA:AA:AA:AA:AA'):
        pkg = Ether(dst=dst, src=src) / "Send Request."
        pkg.type = eval("0xfeed")

        # 网口发送数据
        sendp(pkg, iface=self.iface)


if __name__ == '__main__':

    #### iface 修改为本电脑的网口名称
    item = PackageCapture('Intel(R) 82579LM Gigabit Network Connection')

    # 请求对端发包
    item.req_test()

    # 抓包校验测试
    item.package_cap()


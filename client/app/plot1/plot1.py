import re
import matplotlib.pyplot as plt
import numpy as np


data = [ 47.0613, 47.1776, 47.19998, 46.6697, 42.6231, 42.2377, 41.7452, 33.3073]
data1 = [43.38, 46.53, 38.46, 19.25, 11.84, 3.25, 1.08, 0.57]
data2 = [33.902204, 29.000017, 19.676730, 12.756290, 9.359076, 4.554476, 1.516391, 1.514999] # xquic bbr2+


plt.figure()
loss = ('0', '0.3', '1', '3', '5', '10' , '15', '20')
bar_width = 0.2
index = np.arange(len(loss))  
index_1 = index - 0.2
index_bbr = index
index_xquic = index + 0.2
plt.bar(index_1, height=data, width=bar_width, color='b', label='bbr-udt+')
plt.bar(index_bbr, height=data1, width=bar_width, color='g', label='bbr-udt')
plt.bar(index_xquic, height=data2, width=bar_width, color='r', label='xquic-bbr2+')
# plt.bar(index_bbr, height=data1[rtt], width=bar_width, color='r', label='bbr')
# str1 = rtt.split('_')
# plt.bar(index_udt, height=data1[bandwidth][str1[0]], width=bar_width, color='r', label='udt-vpn')
plt.legend()  
plt.xticks(index, loss)  
plt.xlabel('loss_rate(%)') 
plt.ylabel('goodput (Mbits/s)')  
plt.title("link-bw: 50(Mbits/s)")  #
plt.savefig('50M_BBR.jpg')
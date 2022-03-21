import re
import numpy as np
import matplotlib.pyplot as plt  

num = '50_loss_9'
fp = open(f"data/{num}.txt", 'r')
data = []
for line in fp.readlines():
    data.append(line.split()[0])
data = data[1:]
data = [float(temp) for temp in data]

print(np.average(data))


x = list(range(1, len(data) + 1))

# data2 = [25]* 30 + [50]*30 + [100]*30 + [50]*30 + [25]*29;

data2 = [50] * len(x)

plt.figure()
plt.figure(figsize=(15, 9))
plt.plot(x, data, 'g-', label = f'{num}M-rate',linewidth = 1 )
# plt.plot(x, [num]*len(data), 'r-', label = 'link-rate',linewidth = 1 )
plt.plot(x, data2, 'r-', label = 'link-rate',linewidth = 1 )
# plt.plot(x, data2, 'y-', label = 'bbr-pacing-rate',linewidth = 1 )
plt.title(f'linkrate: {num}M')
plt.xlabel('time(s)')
plt.ylabel('rate(Mbit/s)')
plt.legend()
plt.savefig(f'data/{num}.jpg')
print([50]*60)
import sys
fp = open("file1.csv" , 'r')
dic = {}
for line in fp.readlines():
    number = line.split(',')[0]
    if number not in dic:
        dic[number] = 1
    else:
        dic[number] += 1

retrans = 0
for key in dic:
    if dic[key] > 1:
        retrans += dic[key] - 1

fp2 = open("file2.txt" , 'w')
for key in dic:
    fp2.write(f'{key} {dic[key]}\n')
fp2.close()

if __name__ == "__main__": 
    print(len(dic), retrans, 1500 * 8 * (len(dic) + retrans) / float(sys.argv[1]) / 1e6 )

import os
import math

dfiles = os.listdir('traces')
ts=[]

for name in dfiles:
	t=open('traces/'+name)
	dline=t.readlines()
	ts.append(dline)



#offset=int(ts[0][-1].split(' ')[0])+1
offset=0


for i in range(len(ts)):
	for j in range(len(ts[i])):
		c=int(ts[i][j].split(' ')[0])
		ts[i][j]=ts[i][j].replace(str(c)+' 0x',str(math.floor(c/100)+offset)+' 0x')
		#print(ts[i+1][j])
	offset=int(ts[i][-1].split(' ')[0])+1

f_c=open('c_compresed.trc','w')
for i in ts:
	for j in i:
		f_c.write(j)

f_c.close()







import os

dfiles = os.listdir('traces')
ts=[]

for name in dfiles:
	t=open('traces/'+name)
	dline=t.readlines()
	ts.append(dline)



offset=int(ts[0][-1].split(' ')[0])+1



for i in range(len(ts[1:])):
	for j in range(len(ts[i+1])):
		c=int(ts[i+1][j].split(' ')[0])
		ts[i+1][j]=ts[i+1][j].replace(str(c)+' 0x',str(c+offset)+' 0x')
		#print(ts[i+1][j])
	offset=int(ts[i+1][-1].split(' ')[0])+1

f_c=open('c.trc','w')
for i in ts:
	for j in i:
		f_c.write(j)

f_c.close()







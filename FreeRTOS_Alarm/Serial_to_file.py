# Requirements: serial

import serial
import time
import re
ser = serial.Serial('COM4', 9600)

while 1:
   raw_data = ser.readline()
   data = str(raw_data.decode("utf8"))
   data = data.replace('\r', '')
   print(data)
   f = open('stack_hwm_stats_init&variables_optimized-3.txt','a')
   if re.search("stack*", data):
      if re.search("stackStamp*", data):
         f.truncate(0)
      f.write(data.replace('\r', '')) #data[2:][:-5]
   f.close()
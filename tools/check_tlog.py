# Reads a .tlog with pymavlink (independent reader) and prints what it decodes.
import sys
from pymavlink import mavutil
m = mavutil.mavlink_connection(sys.argv[1], dialect="ardupilotmega", robust_parsing=False)
n = 0
while True:
    msg = m.recv_match()
    if msg is None:
        break
    n += 1
    print(f"{msg._timestamp:.6f} {msg.get_srcSystem()}/{msg.get_srcComponent()} {msg.get_type()} crc_ok={msg.get_msgbuf() is not None}")
print("frames:", n)

#!/usr/bin/python

from struct import *

class SPLLEvent:

# PLLs and branches
    HELPER = 0x1
    MAIN = 0x0
    PRELOCK = 0x2
    PHASE = 0x0
    EXT = 0x2
    EVENT = 0xff
    
    VAL_Y=0
    VAL_ERR=1
    VAL_TAG=2
    VAL_PERIOD=3
    VAL_EVENT=4
    VAL_REF=5
    VAL_SAMPLE_ID=6

    event_map = {
    0 : "",
    1 : "PLL Startup",
    2 : "PLL Locked" }

	EVT_NONE = 0
	EVT_PLL_STARTUP = 1
	EVT_PLL_LOCKED = 2

    source_map = {
        HELPER|PRELOCK : "Helper[prelocking]",
        HELPER : "Helper[phase]",
        PHASE|PRELOCK : "Phase[prelocking]",
        PHASE : "Phase[main]"
    }
 
 
    def __init__(self, rbuf):
        self.y = 0;
        self.err =0;
        self.tag =0;
        self.ref = 0;
        self.event = 0;
        self.source = 0;
        self.channel = 0;
        self.event =0
        self.sample_id=0
        self.kind = 0;
        self.parse(rbuf)
    
    def __str__(self):
        s="%-9d: " % self.sample_id
        if(self.event != 0):
            s="Evt: %s" %self.event_map[self.event];
        
        s=s+" "+self.source_map[self.source];
        s=s+" err %d y %d tag %d ref %d" % (self.err, self.y, self.tag, self.ref)
        
        return s
            
    def parse(self, buf):
        i=0;
#        print(buf)
        while(True):
#            print("Len %d" % len(buf))
            v=buf.pop(0);
            t=(v >> 24) & 0x7f;
#            print("v %x t %x" % (v,t))
            if(t & 0x20):
                self.source = self.source | self.HELPER;
            if(t & 0x40):
                self.source = self.source | self.PRELOCK;
            t=t&0xf

            self.kind = t

            if(t == self.VAL_Y):
                self.y= v & 0xffff;
            if(t == self.VAL_SAMPLE_ID):
                self.sample_id= v & 0xffffff;
            elif(t==self.VAL_ERR):
                self.err = (v & 0xffffff);
                if(self.err > 0x7fffff):
                    self.err = - (0x1000000-self.err)
#                self.err = self.err >> 8
#                if(self.err & 0x800000):
#                    self.err = -self.err;
#                    self.err = self.err & 0x7fffff;
#                    self.err = -self.err;
            elif(t==self.VAL_TAG):
                self.tag = v & 0xffffff;
            elif(t==self.VAL_REF):
                self.ref = v & 0xffffff;
            elif(t==self.VAL_PERIOD):
                self.period = v & 0xfff;
            elif(t==self.VAL_EVENT):
                self.event = v & 0xffffff;
            i=i+1
            if(v & 0x80000000):
                break
#        print(self)
        

class SPLLReader:

    def __init__(self, addr, port):
        import socket
    
        self.sock = socket.socket(
        socket.AF_INET, socket.SOCK_STREAM)
        #now connect to the web server on port 80
        # - the normal http port
        self.sock.connect((addr, port))
        self.events = []
        self.rbuf= []
        self.synced = False
#        self.rbuf.extend( self.read_raw(100, True))

    def read_raw(self, n, resync=False):
        rv=[];
        i=0
        while(i<n):
            chunk = self.sock.recv(8)
            v = unpack('<I', chunk[0:4])[0]
            seq = unpack('<I', chunk[4:8])[0] & 0xffff
#            print("%x %s" % ((v & 0x80000000),synced))
            
            if(self.synced):
                rv.append(v)
                i=i+1
            if((v & 0x80000000 != 0) and (self.synced == False)):
                self.synced = True

#        print("read_raw: %d sync: %d" % (i,self.synced))
                
#        print(len(rv))
        return rv

    def read_events(self, n):
        i=0;
        evts=[]
        while(i<n):
            if(len(self.rbuf) < 10):
                self.rbuf.extend(self.read_raw(100, False))
            evts.append( SPLLEvent(self.rbuf))
            i=i+1
        return evts
        

class LockCycleReader:
    def __init__(self, reader):
        self.reader = reader
    
    def read(self, start_event, src_match, n):
        do_log = False
        retv=[]
        i=0
        while i<n:
            evts = con.read_events(1)
            if(evts[0].event == start_event or start_event == None):
                do_log = True
            if(do_log):
                if(src_match == None or (evts[0].source == src_match)):
                    retv.extend(evts)
                    i=i+1
        return retv


#change the hostname here if you're running the proxy on a different host (i.e. a WR switch)
 con = SPLLReader("localhost", 12345)

reader = LockCycleReader(con)

import matplotlib.pyplot as plt

# read 50000 samples, triggering on the startup of any PLL (for the si570x-demo it's the aux channel only)
evts = reader.read(SPLLEvent.EVT_STARTUP, None, 50000)
#    print("Got");
t = map(lambda x : x.sample_id, evts)
err = map(lambda x : x.err, evts)
tag = map(lambda x : x.tag, evts)
ref = map(lambda x : x.ref, evts)
y = map(lambda x : x.y, evts)

print("Samples: %d\n", len(ref))

plt.plot( err )
plt.plot( y )
plt.ylabel('PLL Response')
plt.show()
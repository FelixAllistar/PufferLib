#!/usr/bin/env python3
# Fake sweep worker: MODE=legacy sends the 784-byte packet; MODE=v2 appends minibatch/learner steps.
import os,struct,sys
args=dict(a[2:].split('=',1) for a in sys.argv[2:] if a.startswith('--') and '=' in a)
fd=int(args['base.result_fd']); lr=float(args['train.learning_rate'])
score=-abs(lr-0.003); cost=1.0; steps=float(args['train.total_timesteps'])
pts=[score]+[0.0]*63; costs=[cost]+[0.0]*63; sp=[steps]+[0.0]*63
data=struct.pack('=fff i 192f',score,cost,steps,1,*pts,*costs,*sp)
if os.environ.get('MODE')=='v2': data+=struct.pack('=ff',1000.0,steps*0.5)
os.write(fd,data); os.close(fd)

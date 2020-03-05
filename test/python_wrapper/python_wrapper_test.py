import sys
import godec
import numpy as np
import gc

def runme():
  ovs = godec.overrides()
  #ovs.add("key", "val")
  
  push_endpoints = godec.push_endpoints()
  push_endpoints.add("convstate")
  push_endpoints.add("raw_audio")
  
  pull_endpoints = godec.pull_endpoints()
  pull_endpoints.add("pull_test", ["preproc_audio", "energy"])
  
  godec.load_godec(sys.argv[1], ovs, push_endpoints, pull_endpoints, True)
  
  num_samples = 16000000
  data = np.random.randn(num_samples).astype('c')
  binary_message = godec.binary_decoder_message(num_samples-1, data, "sample_rate=16000")
  godec.push_message("raw_audio", binary_message)
  
  conv_message = godec.conversation_state_decoder_message(num_samples-1, "utt_id", True, "convo_id", True)
  godec.push_message("convstate", conv_message)
  
  msg_block = godec.pull_message("pull_test", 1000)
  print("new sample rate: "+str(msg_block["preproc_audio"].sample_rate))
  
  godec.blocking_shutdown()
  
runme()


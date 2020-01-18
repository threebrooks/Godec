import sys
import godec
import numpy as np

ovs = godec.overrides()
#ovs.add("key", "val")

push_endpoints = godec.push_endpoints()
push_endpoints.add("convstate")
push_endpoints.add("raw_audio")

pull_endpoints = godec.pull_endpoints()
pull_endpoints.add("pull_test", ["preproc_audio"])

godec.load_godec(sys.argv[1], ovs, push_endpoints, pull_endpoints, True)

data = np.random.randn(16000).astype('c')
binary_message = godec.binary_decoder_message(15999, data, "sample_rate=16000")
godec.push_message("raw_audio", binary_message)

conv_message = godec.conversation_state_decoder_message(15999, "utt_id", True, "convo_id", True)
godec.push_message("convstate", conv_message)

msg_block = godec.pull_message("pull_test", 1000)
print("new sample rate: "+str(msg_block["preproc_audio"].sample_rate))

godec.blocking_shutdown()


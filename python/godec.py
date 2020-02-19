import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'bin'))
import libgodec
import numpy as np

# Godec init structures

class overrides:
  def __init__(self):
    self.ov_dict = {}

  def add(self, key, val):
    self.ov_dict[key] = val

class push_endpoints:
  def __init__(self):
    self.push_list = []

  def add(self, ep):
    self.push_list.append(ep)

class pull_endpoints:
  def __init__(self):
    self.pull_dict = dict()

  def add(self, ep, streams):
    self.pull_dict[ep] = streams

def load_godec(jsonFile, overrides, push_endpoints, pull_endpoints, quiet):
  libgodec.load_godec(jsonFile, overrides, push_endpoints, pull_endpoints, quiet)

def push_message(slot, msg):
  libgodec.push_message(slot, msg)

def pull_message(slot, timeout):
  return libgodec.pull_message(slot, timeout)

def blocking_shutdown():
  libgodec.blocking_shutdown()

# Messages
class decoder_message:
  def __init__(self, time, _type):
    self.time = time
    self.type = _type
    self.descriptor = ""

class nbest_entry:
  def __init__(self, text_array, words_array, alignment_array, confidence_array):
    self.text = text_array
    self.words = words_array
    self.alignment = alignment_array
    self.confidences = confidence_array

class conversation_state_decoder_message(decoder_message):
  def __init__(self, time, utterance_id, last_chunk_in_utt, convo_id, last_chunk_in_convo):
    super().__init__(time, "ConversationStateDecoderMessage")
    self.utterance_id = utterance_id
    self.last_chunk_in_utt = last_chunk_in_utt
    self.convo_id = convo_id
    self.last_chunk_in_convo = last_chunk_in_convo

class audio_decoder_message(decoder_message):
  def __init__(self, time, audio, sample_rate, ticks_per_sample):
    super().__init__(time, "AudioDecoderMessage")
    if ((type(audio) is not np.ndarray) or (audio.dtype != np.float)):
      print("audio_decoder_message: 'audio' field not a numpy float array")
      sys.exit(-1)
    self.audio = audio
    self.sample_rate = sample_rate
    self.ticks_per_sample = ticks_per_sample

class binary_decoder_message(decoder_message):
  def __init__(self, time, data, _format):
    super().__init__(time, "BinaryDecoderMessage")
    self.data = data
    self.format = _format

class nbest_decoder_message(decoder_message):
  def __init__(self, time, entries):
    super().__init__(time, "NbestDecoderMessage")
    self.entries = entries

class features_decoder_message(decoder_message):
  def __init__(self, time, utterance_id, feats, feature_names, feature_timestamps):
    super().__init__(time, "FeaturesDecoderMessage")
    self.utterance_id = utterance_id
    self.feature_names = feature_names
    self.features = feats
    self.feature_timestamps = feature_timestamps

class json_decoder_message(decoder_message):
  def __init__(self, time, json):
    super().__init__(time, "JsonDecoderMessage")
    self.json = json



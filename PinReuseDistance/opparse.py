#!/usr/bin/env python
from __future__ import division
from operator import itemgetter
import sys

miss_event = 'IBS_OP_DATA_CACHE_MISS'

class PCentry:
  def __init__(self, PC, event_names, event_counts, event_fracs):
    self.PC_ = PC
    self.counts_ = {}
    self.fracs_ = {}
    for evt, count, frac in zip(event_names, event_counts, event_fracs):
      self.counts_[evt] = count
      self.fracs_[evt] = frac
  def __str__(self):
    ret = hex(self.PC_) + ': '
    for evt, count in self.counts_.iteritems():
      ret += evt + ':' + str(count) + ',' + str(self.fracs_[evt])
    return ret

def get_events(filename):
  f = open(filename)
  events = []
  found = False
  for line in f:
    toks = line.split()
    if 'Counted' in toks:
      c = toks.index('Counted')
      event = toks[c + 1]
      events.append(event)
      c = [x for x in reversed(toks)].index('count')
      event_count = int(toks[-c])
      found = True
    else:
      if found:
        f.close()
        return events, event_count
  f.close()
  print 'No Events found in', filename
  return None, None

# returns per-pc event counts and map of function name to addr:
#  dict {PC: PCEntry}, dict{name: pc}         
def get_event_counts(events, input_filename):
  inputfile = open(input_filename)
  
  total_count = {}
  total_frac = {}
  PC_counts = {}
  function_map = {}
  for evt in events:
    total_count[evt] = 0
    total_frac[evt] = 0.0

  for line in inputfile:
    toks = line.split()
    #print toks
    line_count = [0 for e in events]
    line_frac = [0.0 for e in events]
    try:
      index = 0
      for evt in events:
        line_count[index] = int(toks[2*index])
        line_frac[index] = float(toks[2*index+1])
        index += 1
      assert(toks[2*index] == ':')
      PC = int(toks[2*index + 1][:-1], 16)
      PC_counts[PC] = PCentry(PC, events, line_count, line_frac)
      #only count the miss if loads are present (specific to getting load miss ratios)
      #if len(events) > 2 or (len(events) > 1 and line_count[1] > 0):
      index = 0
      for evt in events:
        total_count[evt] += line_count[index]
        total_frac[evt] += line_frac[index]
        index += 1
        
    except ValueError:
      if toks[0] == ':':
        #if it's still a PC but with no events, make an empty entry for it
        try:
          PC = int(toks[1][:-1], 16)
          PC_counts[PC] = PCentry(PC, events, line_count, line_frac)
        except (IndexError, ValueError):
          continue
      else:
        try:
          function_addr = int(toks[0], 16)
          function_name = toks[1]
          function_map[function_name] = function_addr
        except ValueError:
          continue

  print 'Total counts for', input_filename
  print total_count, total_frac
  for evt in events:
    print evt, ':', total_count[evt], total_frac[evt]
  if 'IBS_OP_LOAD' in total_count:
    print 'ratio', total_count['IBS_OP_DATA_CACHE_MISS']/total_count['IBS_OP_LOAD']
    print 'full ratio', total_count['IBS_OP_DATA_CACHE_MISS'] / (total_count['IBS_OP_LOAD'] + total_count['IBS_OP_STORE'])

  #for v in PC_counts.itervalues(): print v
  return PC_counts, function_map

# returns per-pc absolute and relative diffs
def get_event_diffs(PC_counts, event):
  # find PCs with the biggest reduction in misses
  PC_diff = {}
  for pc in PC_counts[0]:
    try:
      orig_count = PC_counts[0][pc].counts_[event]
      new_count = PC_counts[1][pc].counts_[event]
      diff = orig_count - new_count
      if orig_count > 0:
        PC_diff[pc] = (diff, diff / orig_count)
      else:
        PC_diff[pc] = (diff, 0)
    except KeyError:
      PC_diff[pc] = (PC_counts[0][pc].counts_[event], 1.0)
  lst = sorted([x for x in PC_diff.iteritems() if x[1] != 0], key=itemgetter(1), reverse=True)
  print lst[:20]
  return PC_diff

def main(argv):
  input_filenames = argv[1:]
  events, event_interval = get_events(input_filenames[0])
  print 'Events:', events, 'interval', event_interval
  assert(events[0] == miss_event)
  PC_counts = []
  function_maps = []
  for input_file in input_filenames:
    evts, interval = get_events(input_file)
    if evts != events:
      print 'events for file', input_file, 'do not match!', evts, events
    counts, map = get_event_counts(events, input_file)
    PC_counts.append(counts)
    function_maps.append(map)
  if len(input_filenames) == 1:
    sys.exit()
  # sanity check function maps
  for name, pc in function_maps[0].iteritems():
    if name not in function_maps[1]:
      print 'function', name, 'not in ', input_filenames[1]
    elif function_maps[1][name] != pc:
      print 'function maps do not match!', pc, name, function_maps[1][pc]

  PC_diff = get_event_diffs(PC_counts, miss_event)
  total_diff = (sum([x[0] for x in PC_diff.itervalues()]) / 
                sum([x.counts_[miss_event] for x in PC_counts[0].itervalues()]))
  print 'overall percent diff = ', total_diff
    

if __name__ == '__main__':
    main(sys.argv) 

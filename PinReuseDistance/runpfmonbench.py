#!/usr/bin/env python

from __future__ import division
import operator
import os
import pickle
import subprocess
from stat import *
import sys

#from opparse import PCEntry

class BenchmarkRunner:
  benchlist = ['cg', 'ft', 'mg', 'genome', 'gafort', 'galgel', 'applu', 
               'apsi', 'swim', 'equake', 'canneal', 'ferret']
  class DataSizes:
    def __init__(self, size):
      self.size = size
      if size == 'train':
        self.nas = 'A'
        self.parsec = 'simlarge'
        self.genome = ('-g16384', '-s64', '-n4194304')
        self.equake = self.swim = self.galgel = self.apsi = self.applu = 'train'
      elif size == 't10':
        self.nas = 'A'
        self.parsec = 'simlarge'
        self.genome = ('-g16384', '-s64', '-n4194304')
        self.applu = self.apsi = self.swim = 'train-10'
        self.equake = 'train-5'
        self.galgel = 'train-20'
        self.size = 'train'
      elif size == 'ref':
        self.nas = 'B'
        self.parsec = 'native'
        self.genome = ('-g32768', '-s128', '-n8388608')
        self.equake = self.swim = self.galgel = self.apsi = self.applu = 'ref'
      else:
        raise ValueError('invalid size')

  def __init__(self, library, size, threads):
    self.library = 'perfctr_' + library
    self.args = self.DataSizes(size)
    self.threads = threads
    os.environ['OMP_NUM_THREADS'] = str(threads)
    if 'EVENTS' not in os.environ:
      os.environ['EVENTS'] = 'CPU_CLK_UNHALTED'
    self.binpath = 'binaries/'
    self.bench_cmds = {}
    for bench in ('cg', 'ft', 'mg'):
      self.bench_cmds[bench] = (self.binpath + bench + '.' + self.args.nas + '-' + self.library,)
    self.bench_cmds['genome'] = (self.binpath + 'genome-' + self.library,) + self.args.genome + \
      ('-t' + threads,)
    for bench in ('gafort', 'galgel', 'applu', 'apsi', 'swim', 'equake'):
      self.bench_cmds[bench] = (self.binpath + bench + '-' + self.library, )
    for bench in ('canneal', 'ferret'):
      self.bench_cmds[bench] = ('parsec-2.1/bin/parsecmgmt', '-a', 'run', '-c',
                                 self.library, '-i', self.args.parsec, '-p', bench, '-n')
    self.bench_cmds['canneal'] += (str(threads), )
    self.bench_cmds['ferret'] += ('1', )
    #print self.bench_cmds
    self.bench_infiles = {}
    #for bench in ('cg', 'ft', 'mg', 'apsi', 'gafort', 'genome', 'canneal', 'ferret'):
    #  self.bench_infiles[bench] = None
    self.bench_infiles['applu'] = self.binpath + self.args.size + '/applu.' + self.args.applu
    self.bench_infiles['galgel'] = self.binpath + self.args.size + '/galgel.' + self.args.galgel
    self.bench_infiles['equake'] = self.binpath + self.args.size + '/equake.' + self.args.equake
    self.bench_infiles['swim'] = self.binpath + self.args.size + '/swim.' + self.args.swim
    
    self.bench_cwd = {}
    self.bench_cwd['canneal'] = self.bench_cwd['ferret'] = 'parsec-2.1' 
    
  def PrepareSimlinks(self):
    try:
      os.remove('gafort.in')
    except OSError:
      pass
    try:
      os.remove('apsi.in')
    except OSError:
      pass
    os.symlink(self.binpath + self.args.size + '/gafort.' + self.args.size,'gafort.in')
    os.symlink(self.binpath + self.args.size + '/apsi.' + self.args.apsi,'apsi.in')
  
  def RunBenchmark(self, bench, pfmon=None):
    if bench not in self.benchlist:
      raise ValueError('unknown benchmark')
    print 'Running', bench, ': cmd', self.bench_cmds[bench], 'input', 
    print self.bench_infiles.get(bench, None)
    outputfile = open(bench+'-out', 'w')
    inputfile = open(self.bench_infiles[bench]) if bench in self.bench_infiles else None
    cwd = self.bench_cwd[bench] if bench in self.bench_cwd else None
    if pfmon is not None:
      command = pfmon.GetCmd(bench, self.bench_cmds[bench])
    else:
      command = self.bench_cmds[bench]
    print command
    process = subprocess.Popen(command, stdin=inputfile,
                            stdout=outputfile, stderr=outputfile)
    status = process.wait()
    outputfile.close()
    if pfmon is not None:
      f = open(pfmon.sample_outfile, 'a')
      print >> f, '#sampleperiod', pfmon.sample_period
      f.close()      
    print bench, 'returned', status      
  
  def RunAllBenchmarks(self, pfmon=None):
    for bench in self.benchlist:
    #for bench in ('canneal', 'ferret'):
    #for bench in ('applu', 'cg', 'genome', 'canneal'):
      self.RunBenchmark(bench, pfmon)

class PfMon:
  benchlist = ['cg', 'ft', 'mg', 'genome', 'gafort', 'galgel', 'applu', 
               'apsi', 'swim', 'equake', 'canneal', 'ferret']
  def __init__(self, library, size, evt=None):
    assert(library == 'env')
    self.start_addr = {'applu':0x4283cf, 'apsi':0x43844f, 'cg':0x040f9bf, 'canneal':0x41584f,
                       'equake':0x041c56f, 'ferret':0x43e37f, 'ft':0x41004f, 'gafort':0x410c0f,
                       'galgel':0x44fe7f, 'genome':0x41062f, 'mg':0x413d7f, 'swim':0x40ef0f}
    self.end_addr = {'applu':0x4284a7, 'apsi': 0x438527, 'canneal':0x415927, 'cg':0x040fa97,
                     'equake':0x41c647, 'ferret':0x43e457, 'ft':0x410127, 'gafort':0x410ce7,
                     'galgel':0x44ff57, 'genome':0x410707, 'mg':0x413e57, 'swim':0x40efe7}
    if size == 'ref':
      self.start_addr['cg'] = 0x040f9ef
      self.end_addr['cg'] = 0x040fac7
      self.start_addr['ft'] = 0x41006f
      self.end_addr['ft'] = 0x410147
      self.start_addr['mg'] = 0x413d7f
      self.end_addr['mg'] = 0x413e57
    self.cmd = 'pfmon-3.9/pfmon/pfmon'
    if size == 't10':
      self.sample_period = 10000
      self.sample_entries = 10000000
    else:
      self.sample_period = 1000000
      self.sample_entries = 1000000
    if evt is None or evt == 'miss':
      self.event = 'MEM_LOAD_RETIRED:L1D_LINE_MISS'
      self.output_suffix = '-samples'
    elif evt == 'inst':
      self.event = 'INST_RETIRED:ANY_P'
      self.output_suffix = '-insts'
    else:
      raise ValueError('bad event value')
  
  def GetCmd(self, bench, command):
    self.sample_outfile = os.path.abspath(bench + self.output_suffix)
    cmd = (os.path.abspath(self.cmd), '--smpl-module=pebs', 
           '--trigger-code-start=' + hex(self.start_addr[bench]),
           '--trigger-code-stop=' + hex(self.end_addr[bench]),
           '--long-smpl-period=' + str(self.sample_period),
           '--short-smpl-period=' + str(self.sample_period),
           '--smpl-entries=' + str(self.sample_entries),
           '-e' + self.event,
           '--smpl-outfile=' + self.sample_outfile
           )
    if bench == 'canneal' or bench == 'ferret':
      fname = bench+'-pfmon-run.sh'
      f = open(fname, 'w')
      for token in cmd:
        print >> f, token,
      print >> f, ' $*'
      f.close()
      os.chmod(fname, os.stat(fname)[0] | S_IXUSR)
      newcommand = command + ('-s', os.path.abspath(fname))
      return newcommand
    return cmd + command

class PCFixer:
  def __init__(self, disfilename, normalize=False):
    #inst_pcs maps PC to index in inst_list
    #inst_list is (PC, line from disassembly)
    self.functions = {}
    self.inst_list = []
    self.norm_list = []
    self.inst_pcs = {}
    self.normalize = normalize
    f = open(disfilename)
    for line in f:
      toks = line.split()
      try:
        if toks[1].endswith('>:'):
          self.functions[toks[1][:-1]] = int(toks[0], 16)
          current_function = toks[1][:-1]
          current_function_pc = int(toks[0], 16)
        elif toks[0].endswith(':'):
          pc = int(toks[0][:-1], 16)
          self.inst_pcs[pc] = len(self.inst_list)
          self.inst_list.append((pc, line))
          offset = pc - current_function_pc
          self.norm_list.append((current_function, offset))
      except (IndexError, ValueError):
        continue
    f.close()

  def GetFixedPC(self, pc):
    if pc not in self.inst_pcs:
      raise KeyError
      print 'pc', hex(pc), 'not found in list of PCs'
      nearest = 0
      while self.inst_list[nearest][0] < pc and nearest < len(self.inst_list):
        nearest += 1
      print 'nearest pc', hex(self.inst_list[nearest][0]), self.inst_list[nearest][1]
      raise KeyError
    idx = self.inst_pcs[pc]
    if self.inst_list[idx][0] != pc:
      raise RuntimeError('PC not found at index')
    if not self.normalize:
      return self.inst_list[idx-1][0]
    else:
      return self.norm_list[idx-1]

  def GetNormalizedPC(self, pc):
    #return the PC as a tuple of (function, offset) so e.g. mg.A can
    # be compared to mg.B
    if pc not in self.inst_pcs:
      raise KeyError
      print 'pc', hex(pc), 'not found in list of PCs'
      nearest = 0
      while self.inst_list[nearest][0] < pc and nearest < len(self.inst_list):
        nearest += 1
      print 'nearest pc', hex(self.inst_list[nearest][0]), self.inst_list[nearest][1]
      raise KeyError
    idx = self.inst_pcs[pc]
    if self.inst_list[idx][0] != pc:
      raise RuntimeError('PC not found at index')
    return self.norm_list[idx]    
  
  def PrintLine(self, pc):
    idx = self.inst_pcs[pc]
    print self.inst_list[idx][1]

class MissProfile:
  benchlist = ['cg', 'ft', 'mg', 'genome', 'gafort', 'galgel', 'applu', 
               'apsi', 'swim', 'equake', 'canneal', 'ferret']
  def __init__(self, benchname=None, samplepath=None, PCDist=None, cachesize=None, name=None):
    self.misses = {}
    self.percent = {}
    self.insts = {}
    self.sample_period = 1
    self.fixer = None
    self.name = name if name is not None else benchname
    if benchname is not None and samplepath is not None:
      try:
        self.fixer = PCFixer('disassemblies/' + benchname + '-perfctr_env')
      except IOError:
        self.fixer = PCFixer('disassemblies/' + benchname + '.A-perfctr_env', normalize=True)
      samplefile = samplepath #+ benchname + '-samples'
      self.ProcessPfmonSamples(samplefile, True)
      try:
        samplefile = samplepath[:samplefile.index('samples')] + 'insts'
        self.ProcessPfmonSamples(samplefile, False)
      except IOError:
        pass

    elif PCDist is not None and cachesize is not None:
      cold_index = 9223372036854775808.000000
      inval_index = 4611686018427387904.000000
      highest_real_dist = 2**62
      for bm in ('cg', 'ft', 'mg'):
        if name.find(bm) != -1:
          self.fixer = PCFixer('disassemblies/' + bm + '.B-perfctr_env')
      for PC, stats in PCDist.iteritems():
        misscount = sum(stats[2][bin][0] for bin in stats[2] 
                        if bin >= cachesize and bin < inval_index)
        #include cold and inval misses
        misscount += sum(stats[2][bin] for bin in stats[2] if bin > highest_real_dist)
        #print PC, stats, misscount
        if self.fixer is not None:
          try:
            PC = self.fixer.GetNormalizedPC(PC)
          except KeyError:
            pass
          
        if misscount > 0:
          self.misses[PC] = misscount
        self.insts[PC] = stats[1]
    else:
      raise ValueError('must specify either benchname and samplapath or '
                       'PCDist and cachesize')
    self.total_misses = sum(self.misses.itervalues())

  def ProcessPfmonSamples(self, samplefile, is_misses):
    f = open(samplefile)
    for line in f:
      #pfmon format is counts, %self, %cum, pc
      if line.startswith('#sampleperiod') or line.startswith('#samplerate'):
        toks = line.split()
        self.sample_period = int(toks[-1])
        continue  
      if line.startswith('#'):
          continue
      toks = line.split()
      pc = int(toks[-1], 16)
      try:
        pc = self.fixer.GetFixedPC(pc)
      except (KeyError, IndexError):
        #print line
        continue
      if is_misses:
        self.misses[pc] = int(toks[0])
        #strip off the '%'
        self.percent[pc] = float(toks[1][:-1])
      else:
        self.insts[pc] =  int(toks[0])
    f.close()
      
  def GetMissPCs(self, use_fraction=True):
    #returns list containing (PC, misscount) or (PC, fraction)
    total_misses = sum(self.misses.itervalues())
    if use_fraction:
      misslist = [(PC, misses / total_misses) for PC, misses in self.misses.iteritems()]
      misslist.sort(key=operator.itemgetter(1), reverse=True)
    else:
      misslist = sorted(self.misses.iteritems(), key=operator.itemgetter(1), reverse=True)
    return misslist
  
  def GetMissTotal(self):
    self.total_misses = sum(self.misses.itervalues())
    return self.total_misses
  
  def PrintTopPCs(self, count=20):
    print self.name
    pcs = self.GetMissPCs(use_fraction=False)
    for pc, misses in pcs[:count]:
      print hex(pc), ':', misses, misses / self.total_misses * 100

class DifferentialProfile:
  def __init__(self, prof1, prof2):
    self.pcdiffs = {}
    if prof1.sample_period != prof2.sample_period:
      raise ValueError('Cant diff profiles with different sample periods')
    self.sample_period = prof1.sample_period
    self.p1misses = prof1.misses
    self.p2misses = prof2.misses
    self.insts = prof1.insts
    for pc, count in prof1.misses.iteritems():
      diff = count - prof2.misses.get(pc, 0)
      self.pcdiffs[pc] = diff
    for pc, count in prof2.misses.iteritems():
      if pc not in self.pcdiffs:
        self.pcdiffs[pc] = -count
  def Dump(self, filename):
    dumpfile = open(filename, 'wb')
    pickle.dump(self, dumpfile)
    dumpfile.close()
  @classmethod
  def Load(cls, filename):
    loadfile = open(filename, 'rb')
    obj = pickle.load(loadfile)
    loadfile.close()
    return obj
  def GetPCRelReductions(self):
    rel_reducs = {}
    general_reduction = 0
    for pc, count in self.pcdiffs.iteritems():
      if pc in self.p1misses:
        rel_reducs[pc] = count / self.p1misses[pc]
      else:
        general_reduction += count
    return rel_reducs
  def AdjustProfileRel(self, profile):
    rel_reducs = self.GetPCRelReductions()
    new_misses = {}
    #print 'Adjusting', profile.name
    for pc, count in sorted(profile.misses.iteritems(), key=operator.itemgetter(1), reverse=True):
      #print hex(pc), ':',
      if pc in rel_reducs and rel_reducs[pc]>0:
        new_count = count - rel_reducs[pc] * count
        new_misses[pc] = new_count
        #print count, rel_reducs[pc] * 100, '->', new_count
      else:
        new_misses[pc] = count
    profile.misses = new_misses
  def AdjustProfileScaled(self, profile):
    #instead of adjusting by relative diff, scale the absolute diff based on samplerate
    #and adjust by absolute
    new_misses={}
    scale = self.sample_period / profile.sample_period
    #print 'Adjusting', profile.name, 'with scale', scale
    for pc, count in sorted(profile.misses.iteritems(), key=operator.itemgetter(1), reverse=True):
      print hex(pc), ':',
      if pc not in self.insts:
        pcscale = 1
      else:
        pcscale =  profile.insts[pc] / (self.insts[pc] * scale)
        print profile.insts[pc], self.insts[pc], pcscale,
      if pc in self.pcdiffs:
        new_count = count - self.pcdiffs[pc] * scale * pcscale
        new_misses[pc] = new_count
        print count, self.pcdiffs[pc] * scale * pcscale, '->', new_count
      else:
        new_misses[pc] = count * pcscale
    profile.misses = new_misses
  def CompareTo(self, other):
    #compare by comparing percent differences for each PC, weighted the frac of the PC in self
    #get normalized references. (normrefs must match for 2 profiles in a diff)
    #get absolute differences for each pc
    #scale absolute differences by ratio of normalized references
    err = 0
    print self.sample_period, other.sample_period
    for pc, diff in self.pcdiffs.iteritems():
      if pc in other.pcdiffs:
        print pc, diff, other.pcdiffs[pc], diff * self.sample_period, other.pcdiffs[pc]*other.sample_period
        err += diff * self.sample_period - other.pcdiffs[pc] * other.sample_period
      else:
        err += diff * self.sample_period
    for pc, diff in other.pcdiffs.iteritems():
      if pc not in self.pcdiffs:
        err -= diff * other.sample_period
    my_total_diffs = sum(self.pcdiffs.itervalues())
    other_total_diffs = sum(other.pcdiffs.itervalues())
    rel_err = err / (my_total_diffs * self.sample_period + other_total_diffs * other.sample_period)
    print 'comparing total_diffs', my_total_diffs, other_total_diffs, 'err', err, 'rel', rel_err

def main(argv):
  if argv[1] == 'run':
    if len(argv) < 4:
      print 'Usage:',argv[0], 'run size threads evt'
      sys.exit(1)
    benchmarks = argv[5:]
    runner = BenchmarkRunner('env', argv[2], argv[3])
    if len(argv) >= 5 and argv[4] == 'inst':
      evt = 'inst'
    else:
      evt = 'miss'
    pfmon = PfMon('env', argv[2], evt)
    runner.PrepareSimlinks()
    if len(benchmarks) == 0:
      runner.RunAllBenchmarks(pfmon)
    else:
      for bm in benchmarks:
        runner.RunBenchmark(bm, pfmon)
  

if __name__ == '__main__':
  main(sys.argv)

#!/usr/bin/env python2.6
from __future__ import division
import collections
import operator
import subprocess
import sys
import rddata

# Class to hold information about a binary, including short name, location of source files, name of binary file
class BinaryInfo:
  benchlist = ['applu','ft','cg', 'equake', 'mg', 'swim', 'apsi', 'gafort', 'galgel', 
              'genome', 'intruder', 'kmeans', 'vacation']
  binary_dir = '/home/dschuff/research/csm/binaries'
  specomp_source_base = '/home/dschuff/research/csm/specomp/benchspec/OMPM2001'
  npb_source_base = '/home/dschuff/research/csm/NPB3.3/NPB3.3-OMP'
  stamp_source_base = '/home/dschuff/research/csm/STM/STAMP'
  source_dir_map = {'applu':specomp_source_base+'/316.applu_m/src',
                    'apsi':specomp_source_base+'/324.apsi_m/src',
                    'cg':npb_source_base+'/CG',
                    'equake':specomp_source_base+'/320.equake_m/src',
                    'ft':npb_source_base+'/FT',
                    'gafort':specomp_source_base+'/326.gafort_m/src',
                    'galgel':specomp_source_base+'/318.galgel_m/src',
                    'genome':stamp_source_base+'/genome',
                    'intruder':stamp_source_base+'/intruder',
                    'kmeans':stamp_source_base+'/kmeans',
                    'mg':npb_source_base+'/MG',
                    'swim':specomp_source_base+'/312.swim_m',
                    'vacation':stamp_source_base+'/vacation'}
  binary_name_map = {'applu':'applu',
                     'apsi':'apsi',
                     'cg':'cg.W',
                     'equake':'equake',
                     'ft':'ft.W',
                     'gafort':'gafort',
                     'galgel':'galgel',
                     'genome':'genome',
                     'intruder':'intruder',
                     'kmeans':'kmeans',
                     'mg':'mg.W',
                     'swim':'swim',
                     'vacation':'vacation'}
  def __init__(self, filename):
    for bench in BinaryInfo.benchlist:
      if filename.find(bench) != -1:
        self.source_dir = BinaryInfo.source_dir_map[bench]
        self.binary = BinaryInfo.binary_dir + '/' + BinaryInfo.binary_name_map[bench]
        self.bench_name = bench
        return
    raise ValueError('Could not match filename ' + filename + ' to a benchmark')

class SourceLocationClass:
  def __new__(self, filename, line, function=None):
    self.file = str(filename)
    self.line = int(line)
    self.function = function
  def __repr__(self):
    return 'SourceLocation("' + self.file + '", ' + str(self.line) + ', "' + str(self.function) + '")'

#Tuple containing file, line, and function info for a PC
SourceLine = collections.namedtuple('SourceLine', 'file, line, function')

#Stores SourceLine info about all seen PCs in a binary to avoid having to call addr2line every time
class BinaryIndex:
  def __init__(self, binary_info):
    self.binary_info = binary_info
    self.cache = {}
    self.filename = binary_info.bench_name + '.index'
    try:
      index_file = open(self.filename)
      self.cache = eval(index_file.read())
      index_file.close()
    except IOError as ex:
      self.cache = {}
  def __del__(self):
    index_file = open(self.filename, 'w')
    index_file.write(repr(self.cache))
    index_file.close()
  def addr2line(self, pc):
    if pc in self.cache:
      return self.cache[pc]
    else:
      self.cache[pc] = self.run_addr2line(pc, self.binary_info)
      return self.cache[pc]
  def run_addr2line(self, pc, binary_info):
    p = subprocess.Popen(['addr2line', '-fe', binary_info.binary, hex(pc)], stdout=subprocess.PIPE)
    output = p.communicate()[0]
    #print output
    try:
      function, file_line, endline = output.split('\n')
      sourcefile, line = file_line.split(':')
      line = int(line)
      #print function, sourcefile, line
    except ValueError as inst:
      print 'Bad output from addr2line: ', output, 'raised', inst
    return SourceLine(sourcefile, line, function)  

def display_source_profile(binary_info, PCDist, library_map):
  #PCCounts = dict(sorted(PCCounts.items(), key=operator.itemgetter(1), reverse=True)[:100])
  #print [(hex(pc),count) for pc,count in PCCounts.iteritems()]
  #dict indexed by SourceLine, containing count for that line
  line_counts = {}
  file_counts = {}
  count_total = 0
  binary_index = BinaryIndex(binary_info)
  #accumulate counts for matching lines
  for pc, (totaldist, count, histogram) in PCDist.iteritems():
    sl = binary_index.addr2line(pc)
    line_counts[sl] = line_counts.setdefault(sl, 0) + count
    if sl.file == '??':
        sl_file = library_map.normalize_pc(pc).image
    else:
        sl_file = sl.file
    file_counts[sl_file] = file_counts.setdefault(sl_file, 0) + count
    count_total += count
  #for sl,count in line_counts.iteritems():
    #print sl, count

  #print file_counts
  for f in sorted(file_counts.iteritems(), key=operator.itemgetter(1), reverse=True):
      print str(f[1] / count_total) + ':', f[0]
  for source_file, file_count in sorted(file_counts.items(), key=operator.itemgetter(1), reverse=True):
    print source_file
    #find locations in this file and sort by line
    file_locations = sorted([sl for sl in line_counts if sl.file == source_file], key=operator.attrgetter('line'))
    #print file_locations
    #dump file lines prepended by ref count
    file_line = 1
    i = 0
    try:
      f = open(source_file)
    except IOError:
      print 'Unable to open file', source_file
      continue
    for line in f:
      count = 0
      #if i < len(file_locations):
      #  print file_line, file_locations[i].line
      #assert(file_line <= file_locations[i].line)
      while i < len(file_locations) and file_locations[i].line == file_line:
        count += line_counts[file_locations[i]]
        i += 1
      print '{0:4}:{1:7}: {2:7.4f} :{3}'.format(file_line, count, count/count_total*100, line),
      file_line += 1
  

def main(argv):
  benchmarks = {}
  for f in argv[1:]:
    benchmarks[f] = rddata.parse_benchmark_file(f)
    try:
        library_map = rddata.LibraryMap(benchmarks[f]['LibraryMap'])
    except KeyError:
        library_map = LibraryMap(None, True)
    #print benchmarks[f]
    si = BinaryInfo(f)
    PCDist = benchmarks[f]['PCDist']
    sample_count = sum([val[1] for val in PCDist.itervalues()])
    print 'sample count',  sample_count, 'total PCs',  len(PCDist)
    PClist = sorted(PCDist.iteritems(), key=lambda x: x[1][1], reverse=True)
    #addr, count = PClist[0]
    #print hex(addr), count
    #print PClist[:100]
    display_source_profile(si, PCDist, library_map)

if __name__ == '__main__':
  main(sys.argv)

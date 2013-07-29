#!/usr/bin/env python2.6

# style note: This was written before I was using a consistent style guideline, but I generally used
# camel case and lowercase for most names. For new code I'm trying to follow Google's style guide,
# but generally trying to keep things consistent within a class or function.

from __future__ import division
import collections
import math
#import matplotlib.pyplot as plot
#import matplotlib
#from matplotlib.font_manager import FontProperties, FontManager
#import mpl_toolkits.mplot3d
import numpy
import operator
import os
import sys

block_bytes = 64

class rddata:
    def __init__(self):
        self.cpus = range(4)
        self.attrs = {'None':None}
        self.data = None
        self.predictions = {} #cpu-> {thread->list}
        self._delimiter = ','
    def __init__(self, initarg, name='unnamed',  cacheSize=1):
        #initarg is map of thread->[data, atrs, predictions, perf]
        #thread[0] is rd histogram data: map size->histoCount
        #thread[1] is attrs: name->val
        #thread[2] is predictions: size->list
        #thread[3] is perf
        self._delimiter = ','
        self.cpus = []
        self.attrs = {}
        self.data = {}

        self.all_histo = {}
        self.all_attrs = {}
        self.read_histo = {}
        self.read_attrs = {}
        self.write_histo = {}
        self.write_attrs = {}
        self.prefetch_histo = {}
        self.prefetch_attrs = {}
        
        self.predictions = {}
        self.name = name
        self.cacheSize = cacheSize
        #shared stacks are lists, not dicts with cpus as their keys, so we make them one
        if isinstance(initarg, list):
            d = {}
            d[1] = initarg
            initarg = d
        if len(initarg) == 0:
            self.isEmpty = True
        else:
            self.isEmpty = False
        self.time_histogram = None#[[] for l in xrange()]
        for cpu in initarg.iteritems(): 
            if isinstance(cpu[1], list):
                #old format: cpu[0] is thread number, [1] is [rd histogram,attrs,predictions, perf]
                new_format = False
                cpu_all_histo = cpu[1][0]
                cpu_all_attrs = cpu[1][1]
            elif isinstance(cpu[1], dict):
                #new format: cpu[0] is thread number, [1] is {'histogram':histogram,
                #'attributes':attributes, 'predictions':predictions, 'perf':perf,
                #'read_histo':{'histogram':histogram, 'attributes':attributes},} etc
                new_format = True
                self.all_histo[cpu[0]] = cpu[1]['histogram']
                self.all_attrs[cpu[0]] = cpu[1]['attributes']
                cpu_all_histo = cpu[1]['histogram']
                cpu_all_attrs = cpu[1]['attributes']
                try:
                    self.read_histo[cpu[0]] = cpu[1]['read_histo']['histogram']
                    self.read_attrs[cpu[0]] = cpu[1]['read_histo']['attributes']
                    self.write_histo[cpu[0]] = cpu[1]['write_histo']['histogram']
                    self.write_attrs[cpu[0]] = cpu[1]['write_histo']['attributes']
                    self.prefetch_histo[cpu[0]] = cpu[1]['prefetch_histo']['histogram']
                    self.prefetch_attrs[cpu[0]] = cpu[1]['prefetch_histo']['attributes']
                except KeyError:
                    pass
            else:
                raise TypeError('invalid cpu data format')
            
            self.attrs[cpu[0]] = cpu_all_attrs
            self.data[cpu[0]] = cpu_all_histo
            if new_format == False:
                if len(cpu[1]) > 3:
                    if self.time_histogram is None:
                        self.time_histogram = cpu[1][2]
                    else:
                        #make the arrays equal size by adding on zero-rows for unreached distances
                        while len(self.time_histogram) < len(cpu[1][2]):
                            self.time_histogram.append([0] * len(self.time_histogram[0]))
                        while len(self.time_histogram) > len(cpu[1][2]):
                            cpu[1][2].append([0] * len(self.time_histogram[0]))
                        for lst in xrange(len(self.time_histogram)):
                            for x in xrange(len(self.time_histogram[lst])):
                                self.time_histogram[lst][x] += cpu[1][2][lst][x]
                    prediction_index = 3 # if time histo data is here, it will be before the predictions
                else:
                    prediction_index = 2
                if len(cpu[1]) > 2:
                    self.predictions[cpu[0]] = cpu[1][prediction_index]
                else:
                    self.predictions[cpu[0]] = None
            else:
                if 'predictions' in cpu[1]:
                    self.predictions[cpu[0]] = cpu[1]['predictions']
                else:
                    self.predictions[cpu[0]] = None
            #combine time reuse histogram data from all threads
            if len(cpu_all_histo) != 0:
                self.cpus.append(cpu[0])
        
        xvals = {}
        for cpu in self.data.itervalues():
            for key in cpu.iterkeys():
                xvals[key] = 1
        self.xvals = sorted(xvals.keys())
        
        try:
          if 'sampleCount' in self.attrs[1]:
            if self.attrs[1]['sampleCount'] != self.attrs[1]['blockAccessCount']:
              print 'sampleCount doesnt match blockAccessCount for', name
        except KeyError:
          pass

    def setDefaultData(self):
        'Use default all-references histogram and attributes'
        self.data = self.all_histo
        self.attrs = self.all_attrs
    def setReadData(self):
        'Use read histogram and attributes instead of all-references histogram'
        self.data = self.read_histo
        self.attrs = self.read_attrs
    def setWriteData(self):
        'Use write histogram and attributes instead of all-references histogram'
        self.data = self.write_histo
        self.attrs = self.write_attrs
    def setPrefetchData(self):
        'Use prefetch histogram and attributes instead of all-references histogram'
        self.data = self.prefetch_histo
        self.attrs = self.prefetch_attrs
        
    def hasPredictions(self):
        return len(self.predictions[1]) > 0
    def getAttr(self, attr, cpu=None, average=False):
        if cpu is None:
            if average == False:
                return sum(self.attrs[x][attr] for x in self.cpus)
            else:
                return (sum(self.attrs[x][attr] * self.getAccessCount([x]) #self.attrs[x]['blockAccessCount'] 
                           for x in self.cpus) / 
                        #sum(self.attrs[x]['blockAccessCount'] for x in self.cpus))
                        self.getAccessCount())
                #return sum(self.attrs[x][attr] for x in self.cpus) / len(self.cpus)
        else:
            return self.attrs[cpu][attr]
    def __getattr__(self, attr):
        return self.getAttr(attr)
    def hasAttr(self, attr):
        return attr in self.attrs[0]
    def getData(self, cpu):
        return self.data[cpu]

    def getAccessCount(self, cpus=None):
        if cpus is None:
            cpus = self.cpus
        if hasattr(self, '_totalAccesses') and cpus == self.cpus:
            return self._totalAccesses
        accessCount = {}
        for cpu in self.cpus:
            accessCount[cpu] = self.attrs[cpu]['sampleCount']
            if ('blockAccessCount' in self.attrs[cpu] and 
              self.attrs[cpu]['blockAccessCount'] != accessCount[cpu]):
                print 'cpu', 0, 'sampleCount didnt match blockAccessCount'
            if sum(self.data[cpu].itervalues()) != accessCount[cpu]:
                print 'cpu', cpu, 'sampleCount didnt match histogram'
        totalAccesses = sum(accessCount.itervalues())
        if cpus == self.cpus:
            self._totalAccesses = totalAccesses
        return totalAccesses 

    def getAverageCDF(self, startXval=0, endIndex=0, cpus=None):
        if hasattr(self, '_avgCDF'): #has to be a better way to do this
            return self._avgCDF
        if cpus is None:
            cpus = self.cpus
        if endIndex == 0:
          endIndex = len(self.xvals)
        cumHitCount = {}
        for cpu in cpus: cumHitCount[cpu] = 0
        accessCount = {}
        cpuCount = float(len(cpus))
        avgHisto = []

        totalAccesses = self.getAccessCount(cpus)

        #for xval in sorted(keys):
        startIndex = 0
        while self.xvals[startIndex] < startXval:
            startIndex += 1
        xvals = self.xvals[startIndex:endIndex]
        for xval in xvals:
            for cpu in cpus:
                if xval in self.data[cpu]:
                    bucketVal = self.data[cpu][xval]
                else:
                    bucketVal = 0
                cumHitCount[cpu] += bucketVal
                
            avgHitRate = sum(cumHitCount.itervalues()) / totalAccesses
            avgHisto.append(avgHitRate)
        #print self.name, avgHisto
        self._avgCDF = xvals, avgHisto
        if xvals[0] == 0:
          xvals[0] = 0.5
        return xvals, avgHisto

    def getNormalizedHistogram(self, **kwargs):
      return self.getHistogram(normalized=True, **kwargs)
    def getHistogram(self, startXval=0, endIndex=0, normalized=False, cpus=None):
        #if hasattr(self, '_histogram') and normalized in self._histogram:
          #return self._histogram[normalized]
        #elif not hasattr(self, '_histogram'):
        #  self._histogram = {}
        if cpus is None:
          cpus = self.cpus
        if endIndex == 0:
          endIndex = len(self.xvals)
        histogram = []
        #totalAccesses = self.getAccessCount(cpus)
        startIndex = 0
        while self.xvals[startIndex] < startXval:
            startIndex += 1
        if startIndex > 0:
            print 'starting x value', startXval,'cuts off', startIndex, 'buckets'
        xvals = self.xvals[startIndex:endIndex]
        for xval in xvals:
          bucketTotal = 0
          for cpu in cpus:
            if xval in self.data[cpu]:
              bucketVal = self.data[cpu][xval]
            else:
              bucketVal = 0
            bucketTotal += bucketVal
          histogram.append(bucketTotal)
        if normalized:
          totalAccesses = sum(histogram)
          print 'normalizing histogram, totalAccesses', totalAccesses
          #print 'getHistogram preNorm', zip(self.xvals, histogram)
          histogram = [x / totalAccesses for x in histogram]
          if sum(histogram) - 1.0 > 0.00000001:
            print 'sum of normalized histogram is ', sum(histogram)
        else:
          totalAccesses = self.getAccessCount(cpus)
        #print 'totalAccesses', totalAccesses
        if totalAccesses < self.getAccessCount(cpus):
          print 'normalizing excludes', 
          print (self.getAccessCount(cpus) - totalAccesses) / self.getAccessCount(cpus) * 100, 
          print '% of accesses' 
        #self._histogram[normalized] = histogram
        #print 'getHistogram', zip(self.xvals, histogram)
        if xvals[0] == 0:
          xvals[0] = 0.5
        return xvals, histogram

    def printTimeHistogram(self):
        print self.time_histogram, len(self.time_histogram)
        space_buckets = [0] + [2**x for x in xrange(len(self.time_histogram) - 1)]
        self.time_buckets = [x / 10 for x in xrange(10)]
        self.time_buckets = range(len(self.time_histogram[0]))
        print space_buckets
        for t in xrange(len(self.time_buckets)):
            print self.time_buckets[t], '| \t',
            for s in xrange(len(space_buckets)):
                print self.time_histogram[s][t], '\t',
            print ''
    def printAttrs(self, outfile, cpus=None):
        if cpus is None:
            cpus = self.cpus
        if len(cpus) == 0: return
        print >> outfile, self.name #+ ':'
        for attr in self.attrs[cpus[0]].iterkeys():
            print >> outfile, attr  + self._delimiter, 
            for cpu in cpus:
                print >> outfile, str(self.attrs[cpu][attr]) + self._delimiter,
            print >> outfile, ''

    def printData(self, outfile, cpus=None):
        if cpus is None:
            cpus = self.cpus
        for cpu in cpus:
            for datum in sorted(self.data[cpu].iteritems()):
                print >> outfile, str(datum[0]) + '\t' + str(datum[1])

    def getPredictionSizes(self):
        if 1 not in self.predictions:
            return []
        return sorted(k for k in self.predictions[1].keys() if isinstance(k, int))

    def getPredHitRatioPerInterval(self, size, cpus=None):
        '''Returns a prediction for each period, averaged over the cpus'''
        if cpus is None:
            cpus = self.cpus
        cpuHits = [self.predictions[x][size] for x in cpus]
        totals = []
        cpuCount = len(cpus)
        if cpuCount == 0:
            return totals
        for i in xrange(len(cpuHits[0])):
            intervalAccesses = sum(self.predictions[x]['accesses'][i] for x in cpus)
            if intervalAccesses > 0:
                totals.append(sum([cpuHits[x][i] for x in xrange(cpuCount)])/ intervalAccesses)
            else:
                totals.append(0)
        return totals
    
    def getAccessTotals(self, cpus=None):
        if cpus is None:
            cpus = self.cpus
        cpuAccesses = [self.predictions[x]['accesses'] for x in cpus]
        totals = []
        if cpuCount == 0:
            return totals
        for i in xrange(len(cpuAccesses[0])):
            totals.append(sum( cpuAccesses[x][i] for x in xrange(cpuCount) ) )
        return totals

    def getPredictionOverallAverageHisto(self,  size,  cpus=None):
        '''Returns predicted hit rate for the whole execution for the given size
         (based on RD histogram)'''
        xvals, cdf = self.getAverageCDF(cpus)
        #print 'gPOAh size',  size
        if size == 0: return 0
        #print xvals, cdf
        for idx in xrange(len(cdf)):
            #print idx, self.xvals[idx], cdf[idx]
            if xvals[idx] > size:
                return cdf[lastIdx-1]
            lastIdx = idx
        return 0
    
    def getPredictionOverallAverage(self, size, cpus=None):
        '''Returns predicted hit ratio for the whole execution for the given size (based on thread predictions)'''
        #size *= block_bytes
        if cpus is None:
            cpus = self.cpus
        totalAccesses = sum(self.attrs[x]['totalPredictionAccesses'] for x in cpus)
        totalHits = sum( [sum(h) for h in [self.predictions[x][size] for x in cpus]])
        #print 'GPOA totalHits/acc/ratio', totalHits, totalAccesses, totalHits / totalAccesses
        totalIntervalAcc = sum( sum(self.predictions[x]['accesses']) for x in cpus)
        if totalAccesses != totalIntervalAcc:
            print 'total interval accesses', totalIntervalAcc,\
                '!= totalPredictionAccesses', totalAccesses
        return totalHits / totalAccesses

    def printPredictions(self, outfile, cpus=None):
        if cpus is None:
            cpus = self.cpus

        #get sizes
        sizes = sorted(self.predictions[1].keys())
        print >> outfile, self.name
        for size in sizes:
            print >> outfile, size
            for cpu in cpus:
                print >> outfile, str(cpu) + self._delimiter,
                for periodPred in self.predictions[cpu][size]:
                    print >> outfile, str(periodPred) + self._delimiter,
                print >> outfile, ''

    def printDataHoriz(self, outfile, stacks=[], cpus=None):
        if cpus is None:
            cpus = self.cpus
        #print >>  outfile, self.name + self._delimiter, #+ ':'

        #averageCDF = self.getAverageHisto()

        cumCount = {}
        accessCount = {}
        cpuCount = float(len(cpus))

        #print column headers
        print >> outfile, self._delimiter,
        for stack in stacks:
            print >> outfile, stack.name + self._delimiter,
            for cpu in xrange(len(cpus)-1):
                print >> outfile, self._delimiter,
        print >> outfile,  self._delimiter, 'averages' #newline
        print >> outfile, self._delimiter,
        for stack in stacks:
            cumCount[stack] = {}
            #thread numbers
            for cpu in cpus:
                cumCount[stack][cpu] = 0
                accessCount[cpu] = self.attrs[cpu]['accessCount']
                print >> outfile, str(cpu) + self._delimiter,
        print >> outfile, self._delimiter,
        for stack in stacks:
            print >> outfile, stack.name + self._delimiter,
        print >> outfile, ''

        #print data
        for xval in self.xvals:
            print >> outfile, str(xval) + self._delimiter,
            avgHitRate = {}
            avgMissRateInv = {}
            for stack in stacks:
                totHitRate = 0.0
                totInvMissRate = 0.0
                for cpu in cpus:
                    try:
                        bucketVal = stack.data[cpu][xval]
                    except KeyError:
                        bucketVal = 0
                    cumCount[stack][cpu] += bucketVal
                    hitRate = float(cumCount[stack][cpu])/float(accessCount[cpu])
                    if hitRate != 1.0: missRateInv = 1 / (1 - hitRate)
                    else: missRateInv = 9999
                    totHitRate += hitRate
                    totInvMissRate += missRateInv
                    print >> outfile, str(hitRate) + self._delimiter,
                    #cumCount[stack][cpu] += bucketVal
                if cpuCount != 0:
                    avgHitRate[stack] = totHitRate / cpuCount
                    avgMissRateInv[stack] = totInvMissRate / cpuCount
            print >> outfile, str(xval) + self._delimiter,
            for stack in stacks:
                if stack in avgHitRate:
                    print >> outfile, str(avgHitRate[stack]) + self._delimiter,
            
            print >> outfile, ''

    def getCDFDiff(self,  other):
        myHisto = self.getAverageCDF()
        otherHisto = other.getAverageCDF()
        #relError = [( (1.0-otherHisto[x]) - (1.0-myHisto[x])) / (1.0-otherHisto[x]) for x in xrange(len(self.xvals)) if otherHisto[x] != 1.0 ]
        def err(y):
            #if self.xvals[y] == 4007346184: return 0
            if otherHisto[y] != 1.0: return ( (1.0-otherHisto[y]) - (1.0-myHisto[y])) / (1.0-otherHisto[y])
            else: return  1.0 - myHisto[y]
        relError = map(err,  xrange(len(self.xvals)) )
        if relError[-1] > 0.01:
            print "relError for CDFDiff is ", relError[-1]
        return relError

    def getHistogramDiff(self, other, startXval=0, endIndex=0, selfcpus=None, othercpus=None):
        xvals1, myHisto = self.getHistogram(normalized=True, 
                                            startXval=startXval, endIndex=endIndex, cpus=selfcpus)
        xvals2, otherHisto = other.getHistogram(normalized=True, 
                                                startXval=startXval, endIndex=endIndex, cpus=othercpus)
        if xvals1 != xvals2:
          print 'Warning: xvals do not match:', self.xvals, other.xvals
        #relError = [( (1.0-otherHisto[x]) - (1.0-myHisto[x])) / (1.0-otherHisto[x]) for x in xrange(len(self.xvals)) if otherHisto[x] != 1.0 ]
        def err(y):
            #if self.xvals[y] == 4007346184: return 0
            if otherHisto[y] != 0: return ( (otherHisto[y]) - (myHisto[y])) / (otherHisto[y])
            else: return  myHisto[y]
        relError = map(err,  xrange(len(myHisto)) )
        absError = [otherHisto[x] - myHisto[x] for x in xrange(len(myHisto))]
        absErrorSum = sum([abs(x) for x in absError])
        print 'absolute error sum, accuracy =\t', absErrorSum, (1 - absErrorSum / 2) * 100
        return xvals1, absError

    def plotDiff(self, other, startXval=0, endIndex=0, show=False):
        xvals, errorDiff = self.getHistogramDiff(other, startXval, endIndex)
        if xvals[-1] > 2**30:
            plotSlice = slice(-1)
        else:
            plotSlice = slice(len(errorDiff))
        #print 'plotDiff', zip(self.xvals, errorDiff)
        fig = plot.figure()
        ax = fig.add_subplot(1, 1, 1)
        ax.plot(xvals[plotSlice],  errorDiff[plotSlice], label='relative error')
        #plot.plot(self.xvals,  myHisto, label=self.name)
        #plot.plot(self.xvals,  otherHisto,  label=other.name)
        #axes = plot.gca()
        ax.set_xscale('log', basex=2)
        ax.legend(loc=2)
        if show:
            plot.show()

    def getOrderStatistic(self, fraction):
        'return the smallest distance longer than the given fraction of all references'
        xvals, cdf = self.getAverageCDF()
        if fraction >= 1.0:
            return xvals[-1]
        index = 0
        while cdf[index] < fraction:
            #last_index = index
            index += 1
        return xvals[index]

class cachedata:
    def __init__(self, initarg, name,  intervalLens,  cacheSize = 1):
        self.name = name
        self.sizes = sorted(initarg.keys())
        self.cpus = sorted(initarg[self.sizes[0]].keys())
        self.data = initarg
        self.intervals = len(initarg[self.sizes[0]][self.cpus[0]])
        self.intervalLens = intervalLens
        self.cacheSize = cacheSize

    def printData(self, outfile):
        print >> outfile, self.name
        for size in self.sizes:
            print >> outfile, size
            for cpu in self.cpus:
                print >> outfile, str(cpu) + self._delimiter,
                for pred in self.data[size][cpu]:
                    print >> outfile, str(pred) + self._delimiter,
                print >> outfile, ''

    def getSizes(self):
        return sorted(self.data.iterkeys())

    def getHitRatioPerInterval(self, size):
        '''Returns list, containing hit ratio for each interval, avged over ALL cpus '''
        cpus = self.cpus
        cpuHits = [self.data[size][x] for x in cpus]
        cpuCount = len(cpus)
        avgList = []

        for interval in xrange(len(cpuHits[0])):
            if self.intervalLens[interval] > 0:
                avgList.append(sum([cpuHits[x][interval] for x in xrange(cpuCount)]) / self.intervalLens[interval])
            else: 
                avgList.append(0)
        return avgList
    
    def getTotalHits(self, size, cpus=None):
        if cpus is None:
            cpus = self.cpus
        cpuHits = sum([sum(self.data[size][x]) for x in cpus])
        
        return cpuHits
    
    def getTotalRefs(self):
        return sum(self.intervalLens)

    def getWeightedAverage(self,  size,  cpus=None):
        avgList = self.getDataAverages(size,  cpus)
        if len(avgList) != len(self.intervalLens):
            print 'intervalLen mismatch in getWeightedAverage'
        totalref = 0
        totalhits = 0.0
        for i in xrange(len(avgList)):
            if avgList[i] == avgList[i]: #not nan
                totalref += self.intervalLens[i]
                totalhits += avgList[i] * self.intervalLens[i]
        print 'totalref',  totalref,  'totalhit',  totalhits,  'totalmiss',  totalref-totalhits
        return totalhits / totalref

    def plotCaches(self, name,  sizes,rdStacks):
        sizeCount = len(sizes)
        xvals = range(self.intervals)
        plotnum = 1
        doD4 = False
        fig = plot.figure(subplotpars=matplotlib.figure.SubplotParams(.05, .05,  .99,  .97,  .18,  .25))
        if 1 in sizes: 
            sizes = sizes[1:]
            doD4 = True
        for size in sizes:
            plot.subplot(2, 2, plotnum)
            if doD4:
                plot.plot(xvals, self.getHitRatioPerInterval(1), label='d4')
            for stack in rdStacks:
                sizeMult = self.cacheSize / stack.cacheSize
                plot.plot(stack.getPredHitRatioPerInterval(int(size/sizeMult)), label=stack.name)
            plot.plot(xvals, self.getHitRatioPerInterval(size), label='gcache')
            plot.title(name + ' cache ' +str(size))
            plot.legend()
            plotnum += 1
        plot.subplot(2, 2, 4)
        plot.plot(self.intervalLens,  label='intervalLens')
        #plot.show()
    
    def getAccuracy(self,  name,  sizes,  rdStacks):
        global block_bytes
        #fig = plot.figure()
        plotnum = 1
        waccMatrix = {} #weighted accuracy matrix
        for stack in rdStacks:
            waccMatrix[stack.name] = {}
        for size in sizes:
            myhits = self.getTotalHits(size)
            myrefs = self.getTotalRefs()
            totalMyMiss = myrefs - myhits

            for stack in rdStacks:
                totalRef= myrefs#= sum(self.intervalLens[startIndex:len(mymisses)])
                sizeMult = self.cacheSize / stack.cacheSize
                histoAvg = 1.0 - stack.getPredictionOverallAverageHisto(int(size/sizeMult/block_bytes))
                predAvg = 1.0 - stack.getPredictionOverallAverage(int(size/sizeMult))
#                if abs(histoAvg - predAvg)/predAvg > 0.01:
#                    print 'DIFF',
#                elif histoAvg == predAvg:
#                    print 'EXACT',

                myAvg = totalMyMiss / totalRef

                #print 'cpu accessCount', sum(stack.attrs[cpu]['accessCount'] for cpu in stack.cpus),\
                #    'cacheRef',  totalRef, 'myhits', myhits
                coherence = sum(stack.getAttr('coherenceMisses', cpu) for cpu in stack.cpus)
                coldMisses = sum(stack.getAttr('coldCount', cpu) for cpu in stack.cpus)
                print 'bench', name,stack.name,'size', size, 'histoavg',  histoAvg, 'predAvg', predAvg,\
                    'myAvg',  myAvg, 'misses', int(predAvg*totalRef), 'coherence', coherence,\
                    str(coherence/predAvg/totalRef*100)+'%', 'cold', coldMisses,\
                    str(coldMisses/predAvg/totalRef*100)+'%'
                mywdiff = abs(predAvg - myAvg) / myAvg

                waccMatrix[stack.name][size] = mywdiff
            #plot.title(name + ' cache ' +str(size))
            #plot.legend()
            plotnum += 1
        #plot.subplot(2, 2, 4)
        #plot.plot(xrange(startIndex,  len(preds)),self.intervalLens[startIndex:len(mymisses)],  label='lens')
        #print accMatrix
        return waccMatrix
            

class PlotProperties:
    benchlist = ['applu','ft','cg', 'equake', 'mg', 'swim', 'apsi', 'gafort', 'galgel', 
                 'genome', 'intruder', 'kmeans', 'vacation', 'ferret', 'canneal']
    def __init__(self,  name,  shortname,  xlower,   xupper,  ylower,  yupper):
        self.shortname = shortname
        self.name = name
        self.xl = 2.0**xlower
        self.xu = 2.0**xupper
        self.yl = ylower
        self.yu = yupper

    @classmethod
    def _create_bench_map(cls, filelist):
        "Create mapping of short benchname to filename in the arglist"
        bm = {}
        for arg in filelist:
            found = False
            for benchname in cls.benchlist:
                if arg.find(benchname) != -1:
                    bm[benchname] = arg
                    found = True
            if found == False:
                bm['unnamed'] = arg #this only works for the last unfound arg
        return bm

    @classmethod
    def create_cdf_properties(cls, filelist):
        benchMap = cls._create_bench_map(filelist)
        bps = {'applu': PlotProperties('316.applu',  'applu',  12.0,  14.8, .952,  1.0), 
               'ft': PlotProperties('FT.A',  'ft',  14.3,  20.1,  .875,  1.0), 
               'cg': PlotProperties('CG.A',  'cg', 13.8,  17.8,  .4, 1.05), 
               'equake': PlotProperties('320.equake',  'equake', 16.5,  18.1, .87, 1.0), 
               'mg': PlotProperties('MG.A',  'mg', 16.45,  21.3,  .75,  1.0), 
               'swim': PlotProperties('312.swim',  'swim', 11.75,  20.5, .42,  1.0), 
               'apsi': PlotProperties('324.apsi',  'apsi', 11.5,  16.5,  .92,  1.01),   
               'gafort': PlotProperties('326.gafort', 'gafort',  10.0,  14.5,  .90,  1.01), 
               'galgel': PlotProperties('318.galgel',  'galgel', 12.3,  16.8,  .95,  1.0),
               'genome': PlotProperties('genome',  'genome',  10, 20, .87, .95),
               'intruder': PlotProperties('intruder',  'intruder',  6, 21, .85, 1),
               'kmeans': PlotProperties('kmeans',  'kmeans',  8, 20, .7, 1),
               'vacation': PlotProperties('vacation',  'vacation',  7, 20, .8, 1),
               'ferret': PlotProperties('ferret', 'ferret', 1, 20, 0, 1),
               'canneal': PlotProperties('canneal', 'canneal', 1, 20, 0, 1),
               'unnamed': PlotProperties('unnamed',  'unnamed',  1, 20, 0, 1)}
        retbps = {}
        #for bench in benchMap:
        #    retbps[benchMap[bench]] = bps[bench]
        retbps['unnamed'] = bps['unnamed']
        for filename in filelist:
            for benchname in bps:
                if filename.lower().find(benchname) != -1:
                    retbps[filename] = bps[benchname]
        return retbps
    
    @classmethod
    def create_timeplot_properties(cls, filelist):
        benchMap = cls._create_bench_map(filelist)
        bps = {'applu': PlotProperties('316.applu',  'applu', 12.75,  14.7, 1e8,  5.5e8), 
               'ft': PlotProperties('FT.W',  'ft', 16.65,  20,  0,  7e9), 
               'cg': PlotProperties('CG.W',  'cg', 16.6,  18.1,  -0.1, 1.2e10), 
               'equake': PlotProperties('320.equake',  'equake', 16.7,  19.8, 1e8, 1.5e9), 
               'mg': PlotProperties('MG.W',  'mg', 18,  21,  0,  2.1e10), 
               'swim': PlotProperties('312.swim',  'swim', 12.2,  20.5, 0,  1.5e10), 
               'apsi': PlotProperties('324.apsi',  'apsi', 13.2,  16.2,  0.2e10,  2.2e10),   
               'gafort': PlotProperties('326.gafort', 'gafort',  9.0,  14.3,  0,  3e10), 
               'galgel': PlotProperties('318.galgel',  'galgel', 11.6,  17,  0,  1e10),
               'genome': PlotProperties('genome',  'genome',  1, 20, 0, 1),
               'intruder': PlotProperties('intruder',  'intruder',  1, 20, 0, 1),
               'kmeans': PlotProperties('kmeans',  'kmeans',  1, 20, 0, 1),
               'vacation': PlotProperties('vacation',  'vacation',  1, 20, 0, 1), 
               'unnamed': PlotProperties('unnamed',  'unnamed',  1, 20, 0, 1)}
        retbps = {}
        for bench in benchMap.iterkeys():
            retbps[benchMap[bench]] = bps[bench]
        return retbps

    @classmethod
    def create_cacheplot_properties(cls, filelist):
        benchMap = cls._create_bench_map(filelist)
        bps = {'applu': PlotProperties('316.applu',  'applu', 12.5,  14.8, .957,  1.0), 
               'ft': PlotProperties('FT.A',  'ft', 14.3,  20.7,  .875,  1.0), 
               'cg': PlotProperties('CG.A',  'cg', 13.8,  17.8,  .25, 1.1), 
               'equake': PlotProperties('320.equake',  'equake', 16.1,  19.4, .86, 1.0), 
               'mg': PlotProperties('MG.A', 'mg',  16.45,  21.3,  .73,  1.0), 
               'swim': PlotProperties('312.swim', 'swim',  11.75,  20.5, .42,  1.0), 
               'apsi': PlotProperties('324.apsi', 'apsi',  11.5,  18.3,  .93,  1.01), 
               'swim': PlotProperties('312.swim', 'swim',  11.75,  20.5, .42,  1.0),  
               'gafort': PlotProperties('326.gafort', 'gafort',  10.4,  16,  .92,  1.01), 
               'galgel': PlotProperties('318.galgel',  'galgel', 12.3,  16.8,  .975,  1.0), 
               'genome': PlotProperties('genome',  'genome',  10, 20, .87, .95),
               'intruder': PlotProperties('intruder',  'intruder',  6, 21, .85, 1),
               'kmeans': PlotProperties('kmeans',  'kmeans',  8, 20, .7, 1),
               'vacation': PlotProperties('vacation',  'vacation',  7, 20, .8, 1),
               'ferret': PlotProperties('ferret', 'ferret', 1, 20, 0, 1),
               'unnamed': PlotProperties('unnamed',  'unnamed',  10, 21,  .4,  1.0)}
        retbps = {}
        for bench in benchMap.iterkeys():
            retbps[benchMap[bench]] = bps[bench]
        return retbps

    @classmethod
    def create_shared_cdf_properties(cls, filelist):
        benchMap = cls._create_bench_map(filelist)
        bps = {'applu': PlotProperties('316.applu', 'applu',  11.5,  16, .9,  1.01), 
               'ft': PlotProperties('FT.A', 'ft',  17.5,  21.5,  .9,  1.0), 
               'cg': PlotProperties('CG.A', 'cg',  15,  20,  .45, 1.05), 
               'equake': PlotProperties('320.equake',  'equake', 16.8,  19.8, .88, 1.0), 
               'mg': PlotProperties('MG.A',  'mg', 20,  23,  .88,  1.0), 
               'swim': PlotProperties('312.swim', 'swim',  19,  22, .75,  1.0), 
               'apsi': PlotProperties('324.apsi', 'apsi',  13,  16.5,  .92,  1.01),   
               'gafort': PlotProperties('326.gafort', 'gafort', 11,  15,  .94,  1.01), 
               'galgel': PlotProperties('318.galgel',  'galgel', 12,  17.5,  .91,  1.01),
                'genome': PlotProperties('genome',  'genome',  10, 20, .87, .95),
               'intruder': PlotProperties('intruder',  'intruder',  6, 21, .85, 1),
               'kmeans': PlotProperties('kmeans',  'kmeans',  8, 20, .7, 1),
               'vacation': PlotProperties('vacation',  'vacation',  7, 20, .8, 1),
               'ferret': PlotProperties('ferret', 'ferret', 1, 20, 0, 1),
               'canneal': PlotProperties('canneal', 'canneal', 1, 20, 0, 1),
               'unnamed': PlotProperties('unnamed',  'unnamed',  1, 20, 0, 1)}
        retbps = {}
        #for bench in benchMap.iterkeys():
        #    retbps[benchMap[bench]] = bps[bench]
        #return retbps
        retbps['unnamed'] = bps['unnamed']
        for filename in filelist:
            for benchname in bps:
                if filename.lower().find(benchname) != -1:
                    retbps[filename] = bps[benchname]
        return retbps



class Options:
    def __init__(self, input_argv):
        global block_bytes
        argv = input_argv * 1
        if len(argv) < 2:
            print '1st arg must be input file'
            sys.exit(1)
        #matplotlib.font_manager.fontManager.set_default_size(5)
        self.cache = False
        self.plot_distance = False
        self.plot_time = False
        self.private = True
        self.shared = False
        self.pair = False
        self.compare = False
        self.old_format = False
        self.multi_axis = False
        self.sampled = False
        self.metrics = False
        self.PDF = False
        self.PCs = False
        self.limits = False
        self.labels = False
        self.histogram_compare_start = 0
        self.time_range = False
        self.pdfcdf = False
        self.miss_ratios = False
        self.plots_per_axes = 2
        self.columns = 2
        self.read_data = False
        self.prefetch_data = False
        self.pcpref = False
        self.dump_diff = False
        self.compare_diff = False
        self.pcratios = False
        if 'cache' in argv:
            self.cache = True
            argv.remove('cache')
        if 'dist' in argv:
            self.plot_distance = True
            argv.remove('dist')
        if 'time' in argv:
            self.plot_time = True
            argv.remove('time')
        if 'shared' in argv:
            self.shared = True
            argv.remove('shared')
        if 'pair' in argv:
            self.pair = True
            argv.remove('pair')
        if 'compare' in argv:
            self.compare = True
            argv.remove('compare')
            print 'Comparing overrides other options.'
            self.plot_distance = True
        if 'oldformat' in argv:
            self.old_format = True
            argv.remove('oldformat')
        if 'multi' in argv:
            #force multi-axis even for just one plot
            self.multi_axis = True
            argv.remove('multi')
        if 'sampled' in argv:
            self.sampled = True
            argv.remove('sampled')
        if 'metrics' in argv:
            self.metrics = True
            argv.remove('metrics')
        if 'pdf' in argv:
            self.PDF = True
            argv.remove('pdf')
        if 'pc' in argv:
            self.PCs = True
            argv.remove('pc')
        if 'limits' in argv:
            self.limits = True
            argv.remove('limits')
        if 'labels' in argv:
            self.labels = True
            argv.remove('labels')
        if 'compstart' in argv:
            startstr = argv[argv.index('compstart') + 1]
            try:
                self.histogram_compare_start = int(startstr)
                del argv[argv.index('compstart') + 1]
            except ValueError:
                print 'argument for compstart missing, using 4'
                self.histogram_compare_start = 4
            argv.remove('compstart')
        if 'timedistance' in argv:
            self.time_range = True
            argv.remove('timedistance')
        if 'pdfcdf' in argv:
            self.pdfcdf = True
            argv.remove('pdfcdf')
        if 'ratios' in argv:
            self.miss_ratios = True
            argv.remove('ratios')
        if 'ppa' in argv:
            ppastr = argv[argv.index('ppa') + 1]
            try:
                self.plots_per_axes = int(ppastr)
                del argv[argv.index('ppa') + 1]
            except ValueError:
                print 'argument for ppa missing, using default'
            argv.remove('ppa')
        if 'cols' in argv:
            colstr = argv[argv.index('cols') + 1]
            try:
                self.columns = int(colstr)
                del argv[argv.index('cols') + 1]
            except ValueError:
                print 'argument for columns missing, using default'
            argv.remove('cols')
        if 'read' in argv:
            self.read_data = True
            argv.remove('read')
        if 'prefetch' in argv:
            self.prefetch_data = True
            argv.remove('prefetch')
        if 'pcpref' in argv:
            self.pcpref = True
            argv.remove('pcpref')
        if 'dumpdiff' in argv:
            self.dump_diff = True
            argv.remove('dumpdiff')
        if 'comparediff' in argv:
            self.compare_diff = True
            argv.remove('comparediff')
        if 'pcratios' in argv:
            self.pcratios = True
            argv.remove('pcratios')
        self.plot =  self.plot_distance or self.plot_time
        self.private = not (self.shared or self.pair or self.sampled)
    
        if len(argv) - 1 > 1 and self.plot:
            self.multi_axis = True

        if (not self.cache and not self.plot and not self.metrics and not self.PCs 
           and not self.miss_ratios and not self.pcpref and not self.pcratios):
            print "You didn't tell me to do anything!"
            print 'usage:', argv[0], '<inputfile> <cache|dist|time|pc|metrics> [shared|pair]'
            sys.exit(1)

        self.files = [self.find_file(f) for f in argv[1:]]

        if '64b' in self.files[0]:
            block_bytes = 64
            print 'using 64 byte blocks'
        elif '4b' in self.files[0]:
            block_bytes = 4
            print 'using 4 byte blocks'
    def find_file(self, prefix):
        try:
            os.stat(prefix)
            # file exists, use it
            return prefix
        except OSError: #no such file; try to find one starting with prefix
            dir, base = os.path.split(prefix)
            filenames = os.listdir(dir)
            real_file = None
            for name in filenames:
                if name.startswith(base):
                    if real_file is not None:
                        raise ValueError("Multiple files with prefix " + prefix)
                    real_file = dir + name
            if real_file is None:
                raise ValueError("Could not find file starting with " + prefix)
            return real_file
    
class LibraryMap:
    NormalizedPC = collections.namedtuple('NormalizedPC', 'image offset')
    def __init__(self, entries, is_empty=False):
        self.__entries = entries
        if entries is None:
            self.__entries = []
        self.__reverse_map = {}
        self.is_empty = is_empty
    def normalize_pc(self, pc):
        for lme in self.__entries:
            if pc >= lme.base and pc <= lme.top:
                ret = self.NormalizedPC(lme.name, pc - lme.base)
                self.__reverse_map[ret] = pc
                return ret
        if not self.is_empty:
            print "warning: couldn't find pc", pc
        return self.NormalizedPC('NONE', pc)
    def get_original_pc(self, normalized_pc):
        if normalized_pc.image == 'NONE':
            return normalized_pc.offset
        return self.__reverse_map[normalized_pc]

def is_rddata_file(filename):
  f = open(filename)
  line = f.readline()
  is_rddata = False
  if line.startswith('#librda') or line.startswith('singleStacks'):
    is_rddata = True
  f.close()
  return is_rddata
    

def parse_benchmark_file(filename):
    myglobals = {'nan':float('nan'),  'inf':float('inf')}
    f = open(filename)
    for line in f:
        if line.startswith('#'):
            try:
                s1 = line.index(' ')
                tag = line[1:s1]
                s2 = line.index(' ',s1+1)
            except ValueError:
                continue
            identifier = line[s1+1:s2]
            #assign = line.split(' ', 2)
            if tag == 'rddata':
                lastid = identifier
                #print 'exec', line[s1+1:]
                exec line[s1+1:] in myglobals
            elif tag == 'preds':
                predsdict = {}
                start = s2
                done = False
                while not done:
                    predslist = []
                    colon = line.find(':', start)
                    key = line[start:colon]
                    try:
                        key = int(key)
                    except ValueError:
                        key = key.strip(" '")
                    start = line.find('[',colon) + 1
                    while True:
                        end = line.find(',',start+1)
                        if end == -1:
                            done = True
                            break
                        try:
                            num = int(line[start:end])
                            predslist.append(num)
                        except ValueError:  #done with line
                            break
                        finally:
                            start = end+1
                    predsdict[key] = predslist
                #print 'append', lastid, predsdict
                try:
                    eval(lastid, myglobals).append(predsdict)
                except AttributeError:
                    #new format: is a dict instead of list
                    eval(lastid, myglobals)['predictions'] = predsdict
            elif tag == 'cache':
                #print 'exec', line[s1+1:]
                exec line[s1+1:] in myglobals
        else:
            exec line in myglobals
    f.close()
    return myglobals

def generic_plot(xvals, yvals, line_labels=None, title=None, scale='log'):
    '''Wrapper around matplotlib plot with options suitable for general looking (but no tweaks for
       publication). plots the each of the elements of xvals and yvals on the same axes, on log
       scale by default.
    '''
    if 'plot' not in globals():
        print 'cant plot: plot library not imported'
        return
    plot.figure()
    if line_labels is None:
        line_labels = [None for x in xvals]
    for i in xrange(len(xvals)):
        plot.plot(xvals[i], yvals[i], label=line_labels[i])
        axes = plot.gca()
        if scale == 'log':
            axes.set_xscale('log', basex=2)
        axes.legend()
    plot.title(title)
    plot.show()

def plot_pc_sample(pc_lists, sample_counts, line_labels):
    'plot the distribution of number of PCs normalized to total samples'
    if 'plot' not in globals(): 
        print ' -not plotting: plot library not imported'
        return
    cdfs = []
    xvals = []
    for i in xrange(len(pc_lists)):
        cdf = []
        cumulative_sum = 0
        for pc in pc_lists[i]:
            cumulative_sum += pc[1][1] / sample_counts[i]
            cdf.append(cumulative_sum)
        cdfs.append(cdf)
        xvals.append(range(len(cdf)))
    generic_plot(xvals, cdfs, line_labels=line_labels, title='PC samplecount dist', scale='linear')
    

def get_miss_pcs(PCCounts, cache_size, slice_size, include_inf=True, use_fraction=True):
    #for a given cache size, rank PCs by those that cause the most misses (count of dist > size)
    #returns list containing (PC, misscount) or (PC, fraction)
    cold_index = 9223372036854775808.000000
    inval_index = 4611686018427387904.000000
    highest_real_dist = 2**62
    miss_fraction = 0.02
    miss_list = []
    for PC, stats in PCCounts.iteritems():
        misscount = sum(stats[2][bin][0] for bin in stats[2] 
                        if bin >= cache_size and bin < inval_index)
        #use the following to include cold and inval misses
        if include_inf:
            misscount += sum(stats[2][bin] for bin in stats[2] if bin > highest_real_dist)
        #print PC, stats, misscount
        if misscount > 0:
            miss_list.append((PC, misscount))
    miss_list.sort(key=operator.itemgetter(1), reverse=True)
    #print miss_list
    if use_fraction:
        total_misses = sum(x[1] for x in miss_list)
        miss_list = [(PC, count / total_misses) for PC, count in miss_list]
    #top_misses = [x for x in miss_list if x[1] >= miss_fraction]
    return miss_list[:slice_size]


def compare_miss_pcs(PCDistList, library_maps):
    compare_count = 30
    cache_sizes = (64,128,256,512)#(64, 128, 256, 512, 1024, 2048)  # kilobytes
    cache_sizes = [x * 1024 / 64 for x in cache_sizes]  # convert to kB with 64 byte lines
    set_list = []  # entry for each PCCounts in PCDist list, containing top_PCs
    print 'Comparing top', compare_count, 'miss-causing PCs'
    for PCCounts, library_map in zip(PCDistList, library_maps):
        top_PCs = []
        for size in cache_sizes:
            top_PCs.append(set(library_map.normalize_pc(pc) for pc, misscount 
                               in get_miss_pcs(PCCounts, size, compare_count)))
            #print 'size', size, [library_map.normalize_pc(pc) for pc, misscount 
            #                   in get_miss_pcs(PCCounts, size, compare_count)]
        print ' Top PC sizes:', [len(s) for s in top_PCs]
        int_set = top_PCs[0]
        print ' cachesize intersection sizes'
        for i in xrange(1, len(cache_sizes)):
            print len(top_PCs[i].intersection(top_PCs[i-1])),
            int_set &= top_PCs[i]
        print ' full intersection', len(int_set) #, int_set
        set_list.append(top_PCs)
    if len(PCDistList) == 2:
        #print set_list
        print ' overlap', [len(set_list[0][i].intersection(set_list[1][i])) for i in xrange(len(cache_sizes))]
  
def compare_pc_sample(pc_dist_list, library_maps):
    PCSets = []
    PClists = []
    sample_counts = []
    sliceSize = 50
    coverage_frac = 0.7
    print 'Comparing PC top', coverage_frac, 'of sample counts'
    for PCCounts, library_map in zip(pc_dist_list, library_maps):
        sample_count = sum([val[1] for val in PCCounts.itervalues()])
        print ' sample count',  sample_count, 'total PCs',  len(PCCounts)
        PClist = sorted(PCCounts.iteritems(), key=lambda x: x[1][1], reverse=True)
        PClists.append(PClist)
        sample_counts.append(sample_count)
        coverage_slice = 0
        covered = 0.0
        while covered < coverage_frac:
            covered += PClist[coverage_slice][1][1] / sample_count
            coverage_slice += 1
        #print [(hex(addr), count) for addr, count in PClist[:sliceSize]]
        PCSet = set(library_map.normalize_pc(addr) for addr, count in PClist[:coverage_slice])
        PCSets.append(PCSet)
        print ' coverage', coverage_slice
    print '', len(PCSets[0].intersection(PCSets[1])), 'of top', coverage_slice, 'in common'
    #print [pc for pc in PCSets[0] if library_maps[0].get_original_pc(pc) > 5000000]
    #print PCSets[0]
    plot_pc_sample(PClists, sample_counts, [str(x) for x in xrange(len(PClists))])
    #create histogram normalized by access count, compare them
    normalizedSets = []
    PCSet = set()
    for PCCounts, library_map in zip(pc_dist_list, library_maps):
        sample_count = sum([val[1] for val in PCCounts.itervalues()])
        normalizedSet = {}
        PCSet.update(library_map.normalize_pc(addr) for addr in PCCounts.iterkeys())
        for pc, dist in PCCounts.iteritems():
            normalizedSet[library_map.normalize_pc(pc)] = dist[1] / sample_count
        normalizedSets.append(normalizedSet)
    totalDiff = sum(abs(normalizedSets[0].get(pc, 0) - normalizedSets[1].get(pc, 0)) for pc in PCSet)
    print 'totalDiff', totalDiff, 'overlap accuracy', (1 - totalDiff / 2) * 100

def compare_pc_distonly(PCDistList, library_maps, dist, access_frac=.8):
    #format is dictionary keyed by PC, value is tuple with (totaldist, accesscount, histogram)
    #histogram is dict keyed by distance, value is (count, avg dist for bin)
    # exception is bin 0 and bin[cold_index] which only have count
    #dist = 1024 * 1024 / 64 
    PCSets = []
    print 'comparing PCs accounting for', access_frac * 100, '% of RDs longer than', dist
    for PCCounts, library_map in zip(PCDistList, library_maps):
        #sort PCs by number of references longer than dist, go down the list till you get enough
        PCSet = set()
        PClist = get_miss_pcs(PCCounts, dist, None, include_inf=False)
        if PCCounts == PCDistList[0]:# or True:#use access_frac for reference set
            distcount = 0
            index = 0
            while distcount < access_frac:
                PC, pc_frac = PClist[index]
                #print PClist[index], library_map.normalize_pc(PC)
                PCSet.add(library_map.normalize_pc(PC))
                #print PCSet
                distcount += pc_frac
                index += 1
                #print PC, library_map.normalize_pc(PC), pc_frac
            print ' access_frac covered by', len(PCSet), 'PCs, out of', len(PClist)
            first_size = len(PCSet)
            #print PCSet
        else:# let it use all the PCs in the reduced set
            #print PClist
            PCSet = set([library_map.normalize_pc(PC) for PC, pc_frac in PClist if pc_frac > 0.0][:first_size])
            print ' full 2nd set contains', len(PCSet), 'PCs'
            #print PCSet
        PCSets.append(PCSet)
    overlap = len(PCSets[0].intersection(PCSets[1]))
    print ' PC intersection', overlap, 'coverage of exact', overlap / len(PCSets[0]) * 100, '%'
    return overlap / len(PCSets[0]) * 100

def compare_weightmatched_nonorm(full_list, sampled_list, dist, access_frac=.8):
    total_misses = sum(x[1] for x in full_list)
    full_misscount = 0
    index = 0
    PCSet = set()
    while full_misscount < access_frac * total_misses:
        PC, pc_count = full_list[index]
        PCSet.add(PC)
        full_misscount += pc_count
        index += 1
    print 'access_frac', access_frac, 'of dist', dist, 'covered by ', len(PCSet), 
    print 'PCs, out of', len(full_list)
    PCdict = dict([(PC, count) for PC, count in full_list ])
    
    sampled_list = [PC for PC, frac in sampled_list][:len(PCSet)]
    samp_misscount = 0
    #print PCdict
    for PC in sampled_list:
        if PC in PCdict:
            samp_misscount += PCdict[PC]
    accuracy = samp_misscount / full_misscount * 100
    print 'full_misscount', full_misscount, 'samp_misscount', samp_misscount, 'accuracy', accuracy 
    return accuracy

def compare_pc_weightmatched(PCDistList, library_maps, dist, access_frac=.8, include_inf=True):
    # choose N such that N PCs cover access_frac of the misses in sampled analysis
    # choose top N entries from sampled list, compute % of misses accounted for by those PCs
    # compare to % of misses accounted for in top N entries of full analysis-
    PCCounts = PCDistList[0]
    PClist = get_miss_pcs(PCCounts, dist, None, 
                                        include_inf=include_inf, use_fraction=False)
    total_misses = sum(x[1] for x in PClist)
    full_misscount = 0
    index = 0
    PCSet = set()
    while full_misscount < access_frac * total_misses:
        PC, pc_count = PClist[index]
        PCSet.add(library_maps[0].normalize_pc(PC))
        full_misscount += pc_count
        index += 1
    print 'access_frac', access_frac, 'of dist', dist, 'covered by ', len(PCSet), 
    print 'PCs, out of', len(PClist)
    PCdict = dict([(library_maps[0].normalize_pc(PC), count) for PC, count in PClist ])
    #cover_size = len(PCSet)
    #print PCSet
    PCCounts = PCDistList[1]
    PClist = get_miss_pcs(PCCounts, dist, None, include_inf=include_inf)[:len(PCSet)]
    PClist = [library_maps[1].normalize_pc(PC) for PC, frac in PClist]
    samp_misscount = 0
    #print PCdict
    for PC in PClist:
        if PC in PCdict:
            samp_misscount += PCdict[PC]
    accuracy = samp_misscount / full_misscount * 100
    print 'full_misscount', full_misscount, 'samp_misscount', samp_misscount, 'accuracy', accuracy 
    return accuracy
    

def compare_pc_dist_reverse(PCDistList, library_maps, dist):
    PCSets = []
    print 'Finding coverage in 1st stacks of PCs in 2nd stacks for RDs longer than', dist

def compare_pc_dist(PCDistList, library_maps):
    #format is dictionary keyed by PC, value is tuple with (totaldist, accesscount, histogram)
    #histogram is dict keyed by distance, value is (count, avg dist for bin)
    # exception is bin 0 and bin[cold_index] which only have count
    PCSets = []
    sliceSize = 100
    frac_threshold = .7
    missThreshold = 256*1024/64 #256k cache divided by element size of 8
    cold_index = 9223372036854775808.000000
    inval_index = 4611686018427387904.000000
    highest_real_dist = 2**62
    accesscount_threshold = 20
    ref_top_drop_fraction = 0.05
    ref_bottom_drop_dist = 4
    PCStats = collections.namedtuple('PCStats', 
        'PC total_distance sample_count avg_distance cold_count histogram')
    paste_string = ''
    print 'Comparing PCs accounting for top', frac_threshold, 'of coldmisses and distance'
    for PCCounts, library_map in zip(PCDistList, library_maps):
        sample_count = sum([val[1] for val in PCCounts.itervalues()])
        total_distance = sum(val[0] for val in PCCounts.itervalues())
        total_coldcount = sum(val[2][cold_index] for val in PCCounts.itervalues())
        print 'sample count',  sample_count, 'total PCs',  len(PCCounts)
        # would like to see fraction of accesses, avg RD, fraction of cold
        pc_stats_list = []
        for PC, counts in PCCounts.iteritems():
            totaldist, accesscount, histogram = counts
            avgdist = totaldist / accesscount if accesscount != 0 else 0
            accessfrac = accesscount / sample_count
            coldcount = histogram[cold_index]
            stats = PCStats(PC, totaldist, accesscount, avgdist, coldcount, histogram)
            #if accesscount >= accesscount_threshold:
            pc_stats_list.append(stats)
        # select top 10% (to start) of cold/coherence, then top 10% of totaldist
        pc_stats_list.sort(key=lambda x: x.cold_count, reverse=True)
        PCSet = set()
        cold_frac = 0
        i = 0
        while cold_frac < frac_threshold and total_coldcount > 0:
            if i >= len(pc_stats_list):
                print 'could not cover', frac_threshold, 'of', total_coldcount, ':cold_frac', cold_frac
                print PCSet
                break
            PCSet.add(library_map.normalize_pc(pc_stats_list[i].PC))
            cold_frac += pc_stats_list[i].cold_count / total_coldcount
            i += 1
        print 'top', frac_threshold, 'of cold/coh misses covered by', i, 'PCs'
        paste_string += str(i) + '\t'
        pc_stats_list = [PC for PC in pc_stats_list if PC.avg_distance > 0]
        def sortkey(stats):
            adjusted_distance = stats.total_distance
            keys = sorted(stats.histogram.iterkeys(), reverse=True)
            # print keys
            # chop off the distances from the top 5% of refs
            refs_to_drop = ref_top_drop_fraction * stats.sample_count
            i = 1
            while refs_to_drop > 0 and i < len(stats.histogram) and keys[i] != 0:
                #print i, keys[i], stats.histogram[keys[i]]
                dropcount = max(refs_to_drop, stats.histogram[keys[i]][0])
                adjusted_distance -= dropcount * stats.histogram[keys[i]][1]
                refs_to_drop -= dropcount
                i += 1
            if refs_to_drop > 0:
                print 'error: failed to drop top 5%:', stats
            # drop the distance total from the bottom few buckets (dist=0,1,2,4)
            i = len(stats.histogram) - 2 #top key is for cold misses
            while i > 1 and stats.histogram[keys[i]] <= ref_bottom_drop_dist:
                adjusted_distance -= stats.histogram[keys[i]][0] * stats.histogram[keys[i]][1]
                i -= 1
            return adjusted_distance

        pc_stats_list.sort(key=sortkey, reverse=True)
        total_adjusted_distance = sum(sortkey(stats) for stats in pc_stats_list)
        dist_frac = 0
        i = 0
        while dist_frac < frac_threshold:
            PCSet.add(library_map.normalize_pc(pc_stats_list[i].PC))
            dist_frac += sortkey(pc_stats_list[i]) / total_adjusted_distance
            i += 1
        print 'top', frac_threshold, 'of dist covered by', i, 'PCs'
        paste_string += str(i) + '\t' + str(len(PCSet)) + '\t'
        print 'set size', len(PCSet)
        PCSets.append(PCSet)

    print 'dist intersection', len(PCSets[0].intersection(PCSets[1]))
    print paste_string + str(len(PCSets[0].intersection(PCSets[1])))

def compare_dist_freq(pc_distance_list, library_maps, benchmark_names):
    #get lists sorted by avg dist and by access count
    plot.figure(subplotpars = matplotlib.figure.SubplotParams(.04, .04, .98, .97, .1, .1))
    cols = 2
    rows = int(math.ceil(len(pc_distance_list) / cols))
    print rows
    index = 1
    for pc_distance_map, library_map, name in zip(pc_distance_list, library_maps, benchmark_names):
        avg_dist_list = sorted(pc_distance_map.iteritems(), reverse=True,
                               key=lambda x: x[1][1] / x[1][0] if x[1][0] != 0 else 0)
        access_count_list = sorted(pc_distance_map.iteritems(), reverse=True, key=lambda x: x[1][1])
        pc_ranks = {}
        for i in xrange(len(avg_dist_list)):
            pc_ranks[avg_dist_list[i][0]] = i
        for i in xrange(len(access_count_list)):
            pc_ranks[access_count_list[i][0]] = (pc_ranks[access_count_list[i][0]], i)
        xvals, yvals = zip(*pc_ranks.itervalues())
        plot.subplot(rows, cols, index)
        plot.scatter(xvals, yvals)
        plot.gca().set_title(name)
        index += 1
    #plot.savefig(benchmark_names[0]+'.pdf',  dpi=300,  format='pdf')
    plot.show()

def plot_3d(rd_data, starttime, endtime):
    #print 'plot_3d', rd_data.time_histogram[1]
    #generic_plot([rd_data.time_buckets], [rd_data.time_histogram[1]], scale='linear')
    rd_data.time_buckets = [x / 10 for x in xrange(10)]
    rd_data.time_buckets = range(len(rd_data.time_histogram[0]))
    if endtime == 0:
        plot_buckets=rd_data.time_buckets[starttime:]
    else:
        plot_buckets=rd_data.time_buckets[starttime:endtime]
    space_bucket_labels = [0] + [2**x for x in xrange(len(rd_data.time_histogram) - 1)]
    space_buckets = range(len(rd_data.time_histogram))
#    fig = plot.figure()
#    ax = mpl_toolkits.mplot3d.Axes3D(fig)
#    for z in xrange(4):
#        xs = numpy.asarray(rd_data.time_buckets)
#        #print xs
#        ys = numpy.asarray(rd_data.time_histogram[z])
#        #print ys
#        ax.bar(xs, ys, zs=space_buckets[z], zdir='y', alpha=0.8, width=0.1)
#    ax.set_xlabel('X')
#    #ax.set_yscale('log', basex=2)
#    ax.set_ylabel('Y')
#    ax.set_zlabel('Z')
#    plot.show()
    
    fig2 = plot.figure()
    ax = mpl_toolkits.mplot3d.Axes3D(fig2)
    xpos, ypos = numpy.meshgrid(plot_buckets, space_buckets[1:])
    xpos = xpos.flatten()
    ypos = ypos.flatten()
    zpos = numpy.zeros_like(xpos)
    dx = 0.8 * numpy.ones_like(zpos)
    dy = 0.8 * numpy.ones_like(zpos)
    dz = numpy.asarray(rd_data.time_histogram[1:], numpy.float32)
    dz = dz.flatten()
    #dz = numpy.asarray([1,2,3,4,5,6,7,8])
    #dz = numpy.ones_like(dx)
    #dz = numpy.asarray([1,1,1,1,1,1,1,1])
    #print dz
    ax.bar3d(ypos, xpos, zpos, dy, dx, dz, color='b')
    ax.set_xlabel('Reuse Distance (2^x)')
    #ax.set_xscale('log', basex=2)
    #ax.set_xticklabels(space_bucket_labels)
    ax.set_ylabel('distance/time factor')
    ax.set_zlabel('Z')
    #plot.show()

def get_miss_ratios(stacks, names):
    sizes = [32 * 1024, 64*1024, 256*1024, 512*1024]
    ratios = {} #name:{size: (M, MR)}
    for stack, name in zip(stacks, names):
        print name
        ratios[name] = {}
        for size in sizes:
            print size, ":",
            ratio = (1 - stack.getPredictionOverallAverageHisto(size / 64)) * 100
            print ratio,
            ratios[name][size] = (ratio, ratio / 100 * stack.getAccessCount()) 
            try:
                print (1 - stack.getPredictionOverallAverage(size)) * 100
            except (KeyError, TypeError):
                print ''
    print 'name', 'refs', 'prefs', '64MR', '64M', '32MR', '32M'
    for stack, name in zip(stacks, names):
        print name, stack.getAccessCount(), stack.prefetchCount if hasattr(stack, 'prefetchCount') else 0,
        for size in (64*1024, 32*1024):
            print ratios[name][size][0], ratios[name][size][1],
        print ''
    return ratios

def print_prefetch_stats(stats):
    if len(stats) == 1: stats = stats[0]
    else: 
        #sum the stats in each thread
        s = {}
        #print stats
        s['prefetches'] = sum([x['prefetches'] for x in stats.itervalues()])
        s['dropped'] = sum([x['dropped'] for x in stats.itervalues()])
        if 'canceled' in s:
            s['canceled'] = sum([x['canceled'] for x in stats.itervalues()])
        s['prefetchers'] = stats[0]['prefetchers']
        del stats[0]
        for p in stats.itervalues():
            for index in range(len(p['prefetchers'])):
                for key, value in p['prefetchers'][index].iteritems():
                    s['prefetchers'][index][key] += value
        #print s
        stats = s
    print 'Totals:', stats['prefetches'], 'prefetches', stats['dropped'], 'dropped',
    if 'canceled' in stats:
        print stats['canceled'], 'canceled'
    else:
        print ''
    for pref in stats['prefetchers']:
        print 'prefetcher', 
        try:
            print pref['name'], 
        except KeyError:
            pass
        print ':', pref['prefetches'], 'prefs', pref['accepted'],
        print '/', pref['dropped'], 'accpt/drp.',
        try:
            print pref['miss_triggers'], '/', pref['prefetch_hit_triggers'], 'miss/hit triggers',
        except KeyError:
            pass
        print ''
        

def main(argv):
    global block_bytes
    options = Options(argv)

    #import plot libraries only if we need them
    if options.plot or options.compare:
        import matplotlib.pyplot as plot
        import matplotlib
        from matplotlib.font_manager import FontProperties, FontManager
        import mpl_toolkits.mplot3d

    #nan and/or inf may appear in the input but python doesn't recognize them unless you wrap thusly
    nan = float('nan')
    inf = float('inf')
    
    #initialize benchmark info that will appear on the plot
    if options.plot_distance or options.compare or options.pcpref:
        if options.private or options.sampled:
            DistProps = PlotProperties.create_cdf_properties(options.files)
        elif options.shared:
            DistProps = PlotProperties.create_shared_cdf_properties(options.files)
        elif options.pair:
            DistProps = PlotProperties.create_shared_cdf_properties(options.files)
    elif options.plot_time:
        DistProps = PlotProperties.create_timeplot_properties(options.files)
    elif options.cache:
        DistProps = PlotProperties.create_cacheplot_properties(options.files)

    benchmarks = {}
    bmextracache = {}
    for f in options.files:
        if options.old_format:
            benchmarks[f] = {'nan':float('nan'),  'inf':float('inf')}
            execfile(f,  benchmarks[f])
        else:
            if is_rddata_file(f):
                benchmarks[f] = parse_benchmark_file(f)
    #sys.exit(0)

    if options.miss_ratios:
        stack = 'simStacks' if options.private else 'simSharedStack'
        rddatas = [rddata(benchmarks[f][stack]) for f in options.files]
        if options.read_data:
            print 'Using only read references for attributes and histograms'
            for stack in rddatas:
                stack.setReadData()
        elif options.prefetch_data:
            print 'Using prefetch references for attributes and histograms'
            for stack in rddatas:
                stack.setPrefetchData()
        get_miss_ratios(rddatas, options.files)
        sys.exit(0)
    if options.PCs:
        stack = 'simStacks' if options.private else 'simSharedStack'
        rddatas = [rddata(benchmarks[f][stack]) for f in options.files]
        try:
            library_maps = [LibraryMap(benchmarks[f]['LibraryMap']) for f in options.files]
        except KeyError:
            library_maps = [LibraryMap(None, True) for f in options.files]
        #compare_miss_pcs([benchmarks[f]['PCDist'] for f in options.files], library_maps)
        #sys.exit(0)
        #print benchmarks[options.files[0]]['LibraryMap']
        #compare_pc_sample([benchmarks[f]['PCDist'] for f in options.files], library_maps)
        #compare_pc_dist([benchmarks[f]['PCDist'] for f in options.files], library_maps)
        #compare_dist_freq([benchmarks[f]['PCDist'] for f in options.files], library_maps, options.files)
        distance_frac = .9
        #access_frac = .8
        overlaps = []
        for access_frac in [.75, .8, .9, .95]:
            print 'using top', (1-distance_frac)*100,'% of distances'
            overlaps.append(compare_pc_weightmatched([benchmarks[f]['PCDist'] for f in options.files], library_maps,
                                rddatas[0].getOrderStatistic(distance_frac), access_frac))
        for o in overlaps:
            print o,
        print ''
        sys.exit(0)
    if options.pcpref:
        #Generate PC differential profiles between arguments, which may be either
        #pfmon output files or rddata output files (note PCDistRead below)

        from runpfmonbench import MissProfile, DifferentialProfile
        cache_size = 32 * 1024 / 64
        #full_file = options.files[0]
        #hw_file = options.files[1]
        pcs = []
        misses = []
        for pc_file in options.files:
          if pc_file in benchmarks:
            prof = benchmarks[pc_file]['PCDistRead']
            pcs.append(MissProfile(PCDist=prof, cachesize=cache_size, name=pc_file))
            misses.append(get_miss_pcs(prof, cache_size, None, use_fraction=False))
          else:
            for bench in MissProfile.benchlist:
              if pc_file.find(bench) != -1:
                prof = MissProfile(bench, pc_file)
                pcs.append(prof)
                break
            misses.append(prof.GetMissPCs(use_fraction=False))
        #for x in range(len(misses[1])):
        #  print misses[0][x], '\t', misses[1][x]
        print options.files[0], options.files[1]
        compare_weightmatched_nonorm(misses[0], misses[1], cache_size, access_frac=.8)
        #compare
        total_misses = [sum(x[1] for x in m) for m in misses]
        diff = total_misses[0] - total_misses[1]
        print 'miss diff:', total_misses, diff, diff / total_misses[0] * 100,'\n'
        diff_prof = DifferentialProfile(pcs[0], pcs[1])
        if options.dump_diff:
          diff_prof.Dump('dumpfile.tmp')
        if options.compare_diff:
          diff2 = DifferentialProfile.Load('dumpfile.tmp')
          diff2.CompareTo(diff_prof)
        #pcs[0].PrintTopPCs(30)
        #pcs[1].PrintTopPCs(30)
        sys.exit(0)
    if options.pcratios:
      cache_size = 32 * 1024 / 64
      from runpfmonbench import MissProfile, DifferentialProfile
      #load rddata, get miss profile. print MR. should match
      for f in options.files:
        rdd = rddata(benchmarks[f]['simStacks'])
        rdd.setReadData()
        prof = MissProfile(PCDist=benchmarks[f]['PCDistRead'], cachesize=cache_size, name=f)
        ratio = 1.0 - rdd.getPredictionOverallAverageHisto(cache_size)
        print 'total misses rddata', ratio, ratio * rdd.getAccessCount(),
        profratio = prof.GetMissTotal() / rdd.getAccessCount()
        print 'profile', profratio, prof.GetMissTotal()
        #load diff, adjust it
        diff = DifferentialProfile.Load('dumpfile.tmp')
        diff.AdjustProfileRel(prof)
        adjustedratio = prof.GetMissTotal() / rdd.getAccessCount()
        print 'adjusted profile', adjustedratio, prof.GetMissTotal(),
        print 'diff', (profratio - adjustedratio) / profratio * 100
        print '(abs)', profratio - adjustedratio, (profratio-adjustedratio)*rdd.getAccessCount(),'\n'
      sys.exit(0)
    if options.metrics:
        stack = 'simStacks'
        print 'doing', stack, 'metrics',
        print 'using read references' if options.read_data else ''
        refs = {}
        prefs = {}
        for f in options.files:
            print '----',f,'----';
            data1 = rddata(benchmarks[f][stack], f)
            if options.read_data: data1.setReadData()
            xvals, pdf = data1.getHistogram()
            distance = sum([xvals[i]*pdf[i] for i in xrange(1,len(pdf)-1)])
            reference_count = data1.getAccessCount()
            print 'total samples', reference_count
            refs[f] = reference_count
            if reference_count != sum(pdf):
              print 'blockAccessCount', reference_count, 'sum', sum(pdf)
            if hasattr(data1, "writeCount"):
              print "  ", reference_count - data1.writeCount - data1.fetchCount, "reads,",
              print data1.writeCount, "writes", data1.fetchCount, "fetches"
              if hasattr(data1, "read_attrs"):
                  if data1.read_attrs[0]['sampleCount'] != (reference_count - data1.writeCount - 
                    data1.fetchCount):
                      print '=>read_attrs sampleCount', data1.read_attrs[0]['sampleCount'] 
            if hasattr(data1, "prefetchCount"):
              print "  ", data1.prefetchCount, "prefetches"
              prefs[f] = data1.prefetchCount
              if data1.prefetchCount > 0: print_prefetch_stats(benchmarks[f]['prefetchStats'])
            if data1.totalDist != distance:
              print 'totalDist', data1.totalDist, distance
            print 'histo avgDist', distance, distance / reference_count
            print 'avgDist', data1.getAttr('avgDist', average=True)
            print 'medianDist', data1.getAttr('medianDist', average=True)
            try:
                adjusted = ( ((data1.coldCount + data1.coherenceMisses) * 500000 + data1.totalDist) 
                            / reference_count)
                print 'incl cold+coherence for 500k', adjusted 
                print 'cold+coherence fraction',
                print (data1.coldCount + data1.coherenceMisses) / reference_count * 100
            except KeyError:
                try:
                    print 'limit+leftover fraction', data1.limitCount / data1.sampleCount * 100
                    #maxCount(inf_dist) = limitCount(prune_count) + invalidationCount
                    print 'inval,limit fraction', data1.invalidationCount / data1.sampleCount * 100,
                    print data1.limitCount / data1.sampleCount * 100
                except KeyError:
                    pass
            xvals, cdf = data1.getAverageCDF()
            print 'bottom frac', cdf[1] * 100
        for f in options.files:
            print '%s %d %d' % (f, refs[f], prefs[f])
        sys.exit()
    if options.time_range:
        stacks = rddata(benchmarks[options.files[0]]['simStacks'],  options.files[0])
        #stacks.printTimeHistogram()
        plot_3d(stacks, 0, 0)
        #plot_3d(stacks, 0, 20)
        #plot_3d(stacks, 20, 0)
        plot.show()
        sys.exit()
    if options.compare:# and False:
        # multi_axis value and plotFunction for plotting on same axes below
        options.multi_axis = True if len(options.files) > options.plots_per_axes else False
        rddata.plotFunction = rddata.getNormalizedHistogram if options.PDF else rddata.getAverageCDF
        histogramStartXval = options.histogram_compare_start
        
        # plot the difference of the first 2 files
        if options.shared:
            stackname = 'simSharedStack'
        else:
            stackname = 'simStacks'
            #stackname = 'singleStacks'
        stack1 = rddata(benchmarks[options.files[0]][stackname],  options.files[0])
        stack2 = rddata(benchmarks[options.files[1]][stackname],  options.files[1])
        if stack1.isEmpty or stack2.isEmpty:
            print 'one of the comparison stacks is empty: stack1', stack1.isEmpty, 'stack2', 
            print stack2.isEmpty
        xvals = set()
        rds = (stack1,  stack2)
        for rd in rds:
            xvals.update(rd.xvals)
        xlist = sorted(list(xvals))

        for rd in rds:
            rd.xvals = xlist

        #stack1.plotDiff(stack2, startIndex=histogramStartIndex)
        stack1.getHistogramDiff(stack2, startXval=histogramStartXval)
        benchIdx = 1
        
        if options.private: 
            #colors = ['red', 'blue']
            #linestyles = [{'c':'red'}, {'c':'blue'}]
            linestyles = [{'c':'black'}, {'c':'.7'}]
        else: 
            linestyles = [{'c':'red'}, {'c':'blue'}, {'c':'blue', 'ls':':'}]
        
        
        #blockSizes = [x * 1024 / 64 for x in (128, 256, 512)]
        #p1 = [stack1.getPredictionOverallAverageHisto(x) for x in blockSizes]
        #p2 = [stack2.getPredictionOverallAverageHisto(x) for x in blockSizes]
        #print blockSizes, p1, p2

    if options.plot:  # figure, legend, axes layout setup
        # The commented out calls are different configurations used in previous versions of the paper
        #fig = plot.figure(figsize = (6.5, 6.7), # 9 dist plots
                          #subplotpars = matplotlib.figure.SubplotParams(.07, .1, .98, .97, .2, .25))
        #fig = plot.figure(figsize = (6.5, 6.8), # 12 dist plots, default size
                          #subplotpars = matplotlib.figure.SubplotParams(.07, .08, .98, .97, .2, .25))
        #fig = plot.figure(figsize = (6.0, 2.1),#reduced time plots
                          #subplotpars = matplotlib.figure.SubplotParams(.07, .27, .98, .91, .2, .25))
        #fig =  plot.figure(figsize = (6.5, 6.0), #smaller PMEO plots
                           #subplotpars = matplotlib.figure.SubplotParams(.07, .08, .98, .97, .2, .25))
        #fig =  plot.figure(figsize = (6.0, 8.75), #all plots, plenty of space in prelim
                           #subplotpars = matplotlib.figure.SubplotParams(.07, .08, .98, .97, .2, .25))
        #fig = plot.figure(figsize = (7.5, 9.5), #all 12 plots
                           #subplotpars = matplotlib.figure.SubplotParams(.07, .07, .98, .97, .17, .17))
        fig = plot.figure(figsize = (5, 5), # adjust as you like
                          subplotpars = matplotlib.figure.SubplotParams(.1, .12, .98, .96, .11, .17))
        legendLines = {}
        legendLabels = {}
    numCols = options.columns
    numRows = math.ceil(math.ceil(len(options.files) / options.plots_per_axes) / numCols)
    plotIndex = 1
    plotCount = options.plots_per_axes
    if options.cache:  # prediction accuracy matrices
        wprivateAccuracy = {}
        wsharedAccuracy = {}
    
    for benchfile in options.files: #do them in the order they were specified in on the command line
    #for benchfile in sorted(options.files): #do them in sorted order
        benchItem = (benchfile, benchmarks[benchfile])
        bench = benchItem[1]
        try:
            benchName = DistProps[benchItem[0]].name #display name (benchItem[0] is file name)
            benchProps = DistProps[benchItem[0]]
        except KeyError:
            benchName = 'unnamed'
            benchProps = DistProps['unnamed']
        
        #select which stacks to plot
        if options.compare:
            if options.shared:
                if 'parseq' in benchfile: name = 'Sequential sampling'
                elif 'sampled' in benchfile: name = 'Parallel sampling'
                else: name = 'Full'
            else:
                if 'sampled' in benchfile: name = 'Sampled'
                elif 'stride' in benchfile: name = 'Prefetch'
                else: name = 'Full'
            #name = benchfile
            plotStack = rddata(bench[stackname], name)
            if options.pdfcdf:
                if plotCount < 2 * options.plots_per_axes:
                    plotStack.plotFunction = plotStack.getNormalizedHistogram
                else:
                    plotStack.plotFunction = plotStack.getAverageCDF
            #plotStack2 = rddata(bench['simSharedStack'],  benchItem[0])
            rds = [plotStack]
            #extraArgs = {'cpus':[benchIdx]}
            extraArgs = {}
            benchIdx += 1
        elif options.private:
            single = rddata(bench['singleStacks'], 'Unaware')
            delayed = rddata(bench['delayStacks'], 'Lazy')
            future = rddata(bench['preStacks'], 'Oracular')
            simulated = rddata(bench['simStacks'], 'Aware')
            rds = [r for r in (single, simulated, delayed, future) if not r.isEmpty]
        elif options.sampled:
            sampled = rddata(bench['simStacks'], 'Sampled')
            rds = [sampled]
        
        cpuCount = len(bench['simStacks'].keys())
        print 'cpuCount', cpuCount
        if cpuCount > 4:
            print 'using compensation for broken pairShared for cpus > 4'

        if options.shared or options.pair:
            single = rddata(bench['singleStacks'], 'Unaware')
            #blocked = rddata(bench['blockStacks'], 'Blocked',  cpuCount)
            #interleaved = rddata(bench['interleaveStacks'], 'Interleaved',  cpuCount)
            simShared = rddata(bench['simSharedStack'], 'Shared',  cpuCount)
            pairShared = rddata(bench['pairStacks'], 'Pairwise Shared',  cpuCount / 2)
            simulated = rddata(bench['simStacks'], 'Aware')
            rds = (single, simShared, pairShared)

        if options.read_data:
            print 'Using read histogram for plotting'
            for rd in rds: rd.setReadData()
        elif options.prefetch_data:
            print 'Using prefetch histogram for plotting'
            for rd in rds: rd.setPrefetchData()

        if options.plot:  # get a common set of x values for all plots and select slice to plot
            xvals = set()
            for rd in rds:
                xvals.update(rd.xvals)
            xlist = sorted(list(xvals))
            for rd in rds:
                rd.xvals = xlist
            if xlist[-2] > 2**48:  #beyond the largest possible actual distance
                plotSlice = slice(0,-2) # last 2 entries are inval misses and cold misses
            else:
                plotSlice = slice(0, -1) # just last entry is cold misses
            #slice(0, None) shows all entries

        if options.cache and len(bench['cacheHits']) > 0:
            if options.private:
                caches = cachedata(bench['cacheHits'], 'simulated caches',  bench['regionAccesses'])
            if options.shared or options.pair:
                shareCaches = cachedata(bench['shareHits'], 'shared cache', 
                                        bench['regionAccesses'],  cpuCount)
                pshareCaches = cachedata(bench['pairHits'], 'pair shared',  
                                         bench['regionAccesses'],  cpuCount / 2)
        else:
            caches = None

        if options.cache:  # get accuracy data for all cache sizes for this benchmark
            if options.private:
                #caches.plotCaches(benchProps.name,  caches.getSizes(),
                                   #(single, simulated, delayed, future))
                wprivateAccuracy[benchProps.name] = caches.getAccuracy(benchProps.name, 
                                                                       caches.getSizes(), rds)
            if options.shared:
                #shareCaches.plotCaches(benchProps.name, shareCaches.getSizes(),
                                        #(blocked, interleaved, simShared))
                wsharedAccuracy[benchProps.name] = shareCaches.getAccuracy(benchProps.name,
                                                        shareCaches.getSizes(), (single, simShared))
            if options.pair:
                #pshareCaches.plotCaches(benchProps.name, pshareCaches.getSizes(), 
                                         #(single, pairShared, simShared))
                wsharedAccuracy[benchProps.name] = pshareCaches.getAccuracy(benchProps.name, 
                                                   pshareCaches.getSizes(), (single,  pairShared,
                                                                              simulated, simShared))

        if options.multi_axis:
            if not options.pdfcdf:
                plot.subplot(numRows, numCols, plotIndex)
            else:
                if plotIndex == 1:
                    ax1 = fig.add_subplot(numRows, numCols, plotIndex)
                else:
                    fig.add_subplot(numRows, numCols, plotIndex, sharex=ax1)

        if options.plot:  # title transform
            #the following lines set the transform which places the axes title text closer to the
            #axes in a highly undocumented way (so may break if matplotlib is upgraded)
            axes = plot.gca()
            axes.titleOffsetTrans = \
                matplotlib.transforms.ScaledTranslation(0.0, 2.0/72, axes.figure.dpi_scale_trans)
            axes.title.set_transform(axes.transAxes + axes.titleOffsetTrans)

        if options.plot_time:  # create time plots
            ncol = 3
            #time is accesses * (1 + penalty * missRatio)
            accessCount = sum([single.getAttr('accessCount', x) for x in single.cpus])
            colors = ['black', '0.4', '0.7', 'red','green','blue','yellow','purple']
            cindex = 0
            for penalty in (10, 100, 200):
                if options.private:
                    xvals, yvals = single.getAverageCDF()
                    time = [accessCount * (1 + penalty * (1 - hr))
                            for hr in yvals]
                    legendLabels[penalty] = single.name+', penalty = '+str(penalty)
                    legendLines[penalty] = plot.plot(xvals[plotSlice], time[plotSlice],
                                                    label=legendLabels[penalty],
                                                    linestyle='-', color=colors[cindex])
                    smallestTime = time[-1]
                    xvals, yvals = simulated.getAverageCDF()
                    time = [accessCount * (1 + penalty * (1 - hr))
                            for hr in yvals]
                    legendLabels[penalty + 1] = simulated.name+', penalty = '+str(penalty)
                    legendLines[penalty+1] = plot.plot(xvals[plotSlice], time[plotSlice],
                                                       label=legendLabels[penalty + 1],
                                                       linestyle=':', color=colors[cindex])
                    timeDiff  =  time[-1] - smallestTime
                    print benchName,  penalty,  timeDiff,  timeDiff / smallestTime,
                    print time[-1]  / smallestTime
                if options.shared:
                    xvals, yvals = simShared.getAverageCDF()
                    time = [accessCount * (1 + penalty * (1 - hr))
                             for hr in yvals]
                    legendLabels[penalty] = simShared.name+', penalty = '+str(penalty)
                    legendLines[penalty] = plot.plot(xvals[plotSlice], time[plotSlice],
                                                     label=legendLabels[penalty],
                                                     linestyle='-', color=colors[cindex])
                    time = [accessCount * (1 + penalty * (1 - hr))
                             for hr in interleaved.getAverageCDF()[plotSlice] ]
                    legendLabels[penalty + 1] = interleaved.name+', penalty = '+str(penalty)
                    legendLines[penalty+1] = plot.plot(xvals, time, label=legendLabels[penalty + 1],
                                                       linestyle=':', color=colors[cindex])
                cindex += 1
                #find index of largest pow2 size that has an entry
            
        if options.plot_distance:  # create distance plots
            if options.compare:
                xvals, yvals = plotStack.plotFunction(startXval=histogramStartXval, **extraArgs)
                #plotSlice = slice(0, len(xvals))
                legendLines[plotCount % options.plots_per_axes] = plot.plot(
                    xvals[plotSlice], yvals[plotSlice], **linestyles[plotCount % options.plots_per_axes])
                legendLabels[plotCount % options.plots_per_axes] = plotStack.name
                #print 'options.compare', zip(xvals, plotStack.plotFunction()[plotSlice])
                ncol = 3
                plotCount += 1
                plotIndex = plotCount // options.plots_per_axes
                
            elif options.private:
                if not single.isEmpty:
                    xvals, yvals = single.getAverageCDF()
                    legendLines[1] = plot.plot(xvals[plotSlice], yvals[plotSlice], c='grey')#c='grey'
                    legendLabels[1] = single.name
                if not simulated.isEmpty:
                    xvals, yvals = simulated.getAverageCDF()
                    legendLines[2] = plot.plot(xvals[plotSlice], yvals[plotSlice], c='k')#c='k'
                    legendLabels[2] = simulated.name
                if not delayed.isEmpty and False:
                    xvals, yvals = delayed.getAverageCDF()
                    legendLines[3] = plot.plot(xvals[plotSlice], yvals[plotSlice],  c='k', ls=':')
                    legendLabels[3] = delayed.name
                if not future.isEmpty and False:
                    xvals, yvals = future.getAverageCDF()
                    legendLines[4] = plot.plot(xvals[plotSlice], yvals[plotSlice],  c='k',  ls='-.')
                    legendLabels[4] = future.name
                ncol = len(legendLines.keys())
            elif options.sampled:
                legendLines[1] = plot.plot(xvals, sampled.getAverageCDF()[plotSlice], c='blue')#c='grey'
                legendLabels[1] = sampled.name
                ncol = 1
            elif options.shared:
                ncol = 2
                #legendLines[1] = plot.plot(xvals, blocked.getAverageCDF()[plotSlice], c='k', ls='--')
                #legendLabels[1] = blocked.name
                #legendLines[2]  = plot.plot(xvals, interleaved.getAverageCDF()[plotSlice],  c='k', ls=':')
                #legendLabels[2] = interleaved.name
                xvals, yvals = simShared.getAverageCDF()
                legendLines[3]  = plot.plot(xvals[plotSlice], yvals[plotSlice],  c='k')
                legendLabels[3] = simShared.name
                if not single.isEmpty:
                    xvals, yvals = single.getAverageCDF()
                    legendLines[4] = plot.plot([x * 4 for x in xvals][plotSlice], yvals[plotSlice], c='grey')
                    legendLabels[4] = single.name
                if options.pair:
                    ncol = 3
                    xvals, yvals = pairShared.getAverageCDF()
                    legendLines[5] = plot.plot([x * 2 for x in xvals][plotSlice], yvals[plotSlice],
                                               label='pairShared', linestyle=':',c='k')
                    legendLabels[5] = pairShared.name
                    #plot.legend(loc=2,  prop=FontProperties(size='small'))
            plot.title(benchName)
        
        if options.plot:  # set scale, title axis limits and tick labels
            axes = plot.gca()
            axes.set_xscale('log', basex=2)
            axes.set_title(benchName)#,  size='small')#
            if options.private and options.limits:
                axes.set_xlim(benchProps.xl,  benchProps.xu)
                axes.set_ylim(benchProps.yl,  benchProps.yu)
                print benchProps.xl, benchProps.xu, benchProps.name
            #used for the plot of FT in shared results of PACT10
            if options.pdfcdf:
                axes.set_xlim(1, 33)
                if plotCount -1 < 2 * options.plots_per_axes:#pdf
                    axes.set_ylim(0, .25)
                    plot.title('Histogram')
                else:
                    axes.set_ylim(0, .87)
                    plot.title('CDF')
                    ##axes.set_xlim(24,6000000)
            ticklist = []
            ticklen = len(axes.get_xticks())
            t = 0
            for x in axes.get_xticks():
                if x == 0: tick = '0'
                if x == 0.5: tick = '0'
                if x < 1024: tick = str(int(x))
                elif x < 1024*1024: tick = str(int(x/1024))+'k'
                else: tick = str(int(x/1024/1024))+'M'
                tick = tick if (ticklen < 8 or t % 2 == 1) else ''
                t += 1
                ticklist.append(tick)
            if options.labels:
                axes.set_xticklabels(ticklist)#,  size='small')
                axes.set_yticklabels([str(x) for x in axes.get_yticks()],  fontsize='small' )
            if options.shared and not single.isEmpty:
                for psz in zip(single.getPredictionSizes(), simShared.getPredictionSizes()):
                    singlex = int(psz[0] / block_bytes)
                    sharedx = int(psz[1] / block_bytes)
                    print 'line at', singlex, math.log(singlex, 2)
                    #axes.axvline(x=int(singlex))
                    singley = single.getPredictionOverallAverageHisto(singlex)
                    sharedy = simShared.getPredictionOverallAverageHisto(sharedx)
                    #plot.arrow(singlex, singley ,sharedx-singlex, sharedy-singley, ec='red',fc='red' )

        if not options.compare:
            plotIndex += 1

    if options.cache:  # format and print cache prediction accuracy
        if options.private:
            accuracy = wprivateAccuracy
        if options.shared or options.pair:
            accuracy = wsharedAccuracy
            
        #get average for each type in each bench
        if options.private:
            stacktypes = ['Unaware',  'Aware']
            if True or not delayed.isEmpty:
              stacktypes.extend([ 'Lazy',  'Oracular'])
        elif options.shared:
            stacktypes = ['Unaware',   'Shared']#,  'Blocked',  'Interleaved']
        elif options.pair:
            stacktypes = ['Unaware',  'Pairwise Shared',  'Aware', 'Shared']
        print 'percent error by benchmark'
        print 'benchmark\t\t', 
        for stacktype in stacktypes: print stacktype, '\t\t', 
        print ''
        for benchName,  benchAcc in accuracy.iteritems():
            print benchName, '\t', 
            for stacktype in stacktypes:
                try:
                    sizes = benchAcc[stacktype]
                    avg = sum(sizes.itervalues()) / len(sizes) 
                    print '\t%.5f'  %  (avg*100), 
                except KeyError:
                    print '\tN/A',
            print ''
            lastBench = benchName
            
        #get average for each type for each size
        print 'percent error by size'
        sizes = accuracy[lastBench][stacktypes[0]].keys()
        print 'size\t', 
        for size in sizes: print size,  '\t',
        if options.private: print '\t', 
        print ''
        for stacktype in stacktypes:
            print stacktype, 
            for size in sizes:
                asum = 0
                count = len(accuracy)
                for benchAcc in accuracy.itervalues():
                    try:
                        asum += benchAcc[stacktype][size]
                    except KeyError:
                        count -= 1
                print '\t%.5f' % (float(asum) / count * 100), 
            print ''

    if options.plot:  # apply figure legend and save as PDF
        linelist = [legendLines[x] for x in sorted(legendLines.iterkeys())]
        labellist = [legendLabels[x] for x in sorted(legendLabels.iterkeys())]
        leg = plot.figlegend(linelist,  labellist, 'lower center', prop=FontProperties(size='small'),
                           markerscale=0.8, ncol=ncol)#,  
        plot.xlabel('Reuse distance (64-byte blocks)', fontsize='medium')
        if options.PDF:
            plot.ylabel('Fraction of references', fontsize='medium')
        else:
            plot.ylabel('Fraction < X', fontsize='medium')
        plot.savefig('test.pdf',  dpi=300,  format='pdf')
        plot.savefig('plot.png', dpi=300, format='png')
        plot.show()

if __name__ == '__main__':
    main(sys.argv)

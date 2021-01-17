/////////////////////////////////////////////////////////////////////////////
//
// BSD 3-Clause License
//
// Copyright (c) 2019, University of California, San Diego.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "SinkClustering.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>

static const char* USE_EXTERNAL_SINK_CLUSTER
    = std::getenv("USE_EXTERNAL_SINK_CLUSTER");

namespace cts {

void SinkClustering::normalizePoints(float maxDiameter)
{
  double xMax = -std::numeric_limits<double>::infinity();
  double xMin = std::numeric_limits<double>::infinity();
  double yMax = -std::numeric_limits<double>::infinity();
  double yMin = std::numeric_limits<double>::infinity();
  for (const Point<double>& p : _points) {
    xMax = std::max(p.getX(), xMax);
    yMax = std::max(p.getY(), yMax);
    xMin = std::min(p.getX(), xMin);
    yMin = std::min(p.getY(), yMin);
  }

  double xSpan = xMax - xMin;
  double ySpan = yMax - yMin;
  for (Point<double>& p : _points) {
    double x = p.getX();
    double xNorm = (x - xMin) / xSpan;
    double y = p.getY();
    double yNorm = (y - yMin) / ySpan;
    p = Point<double>(xNorm, yNorm);
  }
  _maxInternalDiameter = maxDiameter / std::min(xSpan, ySpan);
}

void SinkClustering::computeAllThetas()
{
  for (unsigned idx = 0; idx < _points.size(); ++idx) {
    const Point<double>& p = _points[idx];
    double theta = computeTheta(p.getX(), p.getY());
    _thetaIndexVector.emplace_back(theta, idx);
  }
}

void SinkClustering::sortPoints()
{
  std::sort(_thetaIndexVector.begin(), _thetaIndexVector.end());
}

double SinkClustering::computeTheta(double x, double y) const
{
  if (isOne(x) && isOne(y)) {
    return 0.5;
  }

  unsigned quad = numVertex(std::min(unsigned(2.0 * x), (unsigned) 1),
                            std::min(unsigned(2.0 * y), (unsigned) 1));

  double t = computeTheta(2 * std::fabs(x - 0.5), 2 * std::fabs(y - 0.5));

  if (quad % 2 == 1) {
    t = 1 - t;
  }

  double integral;
  double fractal = std::modf((quad + t) / 4.0 + 7.0 / 8.0, &integral);
  return fractal;
}

unsigned SinkClustering::numVertex(unsigned x, unsigned y) const
{
  if ((x == 0) && (y == 0)) {
    return 0;
  } else if ((x == 0) && (y == 1)) {
    return 1;
  } else if ((x == 1) && (y == 1)) {
    return 2;
  } else if ((x == 1) && (y == 0)) {
    return 3;
  }

  std::cout << "[ERROR] Invalid parameters in " << __func__ << "\n";
  std::exit(1);
}

void SinkClustering::findBestMatching()
{
  std::vector<Matching> matching0;
  std::vector<Matching> matching1;
  double matchingCost0 = 0.0;
  double matchingCost1 = 0.0;

  for (unsigned i = 0; i < _thetaIndexVector.size() - 1; ++i) {
    unsigned idx0 = _thetaIndexVector[i].second;
    unsigned idx1 = _thetaIndexVector[i + 1].second;
    Point<double>& p0 = _points[idx0];
    Point<double>& p1 = _points[idx1];

    double cost = p0.computeDist(p1);
    if (i % 2 == 0) {
      matching0.emplace_back(idx0, idx1);
      matchingCost0 += cost;
    } else {
      matching1.emplace_back(idx0, idx1);
      matchingCost1 += cost;
    }
  }

  // Cost 1 needs to sum the distance from first to last
  // element
  unsigned idx0 = _thetaIndexVector.front().second;
  unsigned idx1 = _thetaIndexVector.back().second;
  matching1.emplace_back(idx0, idx1);
  Point<double>& p0 = _points[idx0];
  Point<double>& p1 = _points[idx1];
  matchingCost1 += p0.computeDist(p1);

  std::cout << "Matching0 size: " << matching0.size() << "\n";
  std::cout << "Matching1 size: " << matching1.size() << "\n";
  if (matchingCost0 < matchingCost1) {
    _matchings = matching0;
  } else {
    _matchings = matching1;
  }
}

void SinkClustering::run()
{
  normalizePoints();
  if (USE_EXTERNAL_SINK_CLUSTER != nullptr) {
    std::ofstream file("nodeInfo.csv");
    std::stringstream ss;
    file << "NodeLocX,NodeLocY\n";
    for (auto& pt : _points)
      file << pt.getX() << "," << pt.getY() << "\n";
    file.close();
    ss << USE_EXTERNAL_SINK_CLUSTER
       << " -i nodeInfo.csv -o clusterId.txt -s "
          "30 -d "
       << _maxInternalDiameter;
    std::cout << "EXECUTING COMMAND : " << ss.str() << std::endl;
    int retVal = std::system(ss.str().c_str());
    if (retVal == 0) {
      int nodeId, nodeCluster;
      int prevNodeCluster = -1;
      std::ifstream ifs("clusterId.txt");
      _kMeanClusterSolution.clear();
      std::vector<unsigned> curSoln;
      while (ifs >> nodeId >> nodeCluster) {
        if (nodeCluster != prevNodeCluster) {
          if (curSoln.empty() == false)
            _kMeanClusterSolution.push_back(curSoln);
          curSoln.clear();
        }
        curSoln.push_back(nodeId);
        prevNodeCluster = nodeCluster;
      }
      ifs.close();
      return;
    }
  }
  computeAllThetas();
  sortPoints();
  findBestMatching();
  writePlotFile();
}

void SinkClustering::run(unsigned groupSize, float maxDiameter)
{
  normalizePoints(maxDiameter);
  if (USE_EXTERNAL_SINK_CLUSTER != nullptr) {
    std::ofstream file("nodeInfo.csv");
    std::stringstream ss;
    file << "NodeLocX,NodeLocY\n";
    for (auto& pt : _points)
      file << pt.getX() << "," << pt.getY() << "\n";
    file.close();
    ss << USE_EXTERNAL_SINK_CLUSTER << " -i nodeInfo.csv -o clusterId.txt -s "
       << groupSize << " -d " << _maxInternalDiameter;
    std::cout << "EXECUTING COMMAND : " << ss.str() << std::endl;
    int retVal = std::system(ss.str().c_str());
    if (retVal == 0) {
      int nodeId, nodeCluster;
      int prevNodeCluster = -1;
      std::ifstream ifs("clusterId.txt");
      _kMeanClusterSolution.clear();
      std::vector<unsigned> curSoln;
      while (ifs >> nodeId >> nodeCluster) {
        if (nodeCluster != prevNodeCluster) {
          if (curSoln.empty() == false)
            _kMeanClusterSolution.push_back(curSoln);
          curSoln.clear();
        }
        curSoln.push_back(nodeId);
        prevNodeCluster = nodeCluster;
      }
      ifs.close();
      return;
    }
  }
  computeAllThetas();
  sortPoints();
  findBestMatching(groupSize);
  writePlotFile(groupSize);
}

std::vector<std::vector<unsigned>> SinkClustering::sinkClusteringSolution()
{
  if (USE_EXTERNAL_SINK_CLUSTER == nullptr || _kMeanClusterSolution.empty())
    return _bestSolution;
  return _kMeanClusterSolution;
}

void SinkClustering::writePlotFile()
{
  std::ofstream file("plot.py");
  file << "import numpy as np\n";
  file << "import matplotlib.pyplot as plt\n";
  file << "import matplotlib.path as mpath\n";
  file << "import matplotlib.lines as mlines\n";
  file << "import matplotlib.patches as mpatches\n";
  file << "from matplotlib.collections import PatchCollection\n\n";

  for (unsigned idx = 0; idx < _thetaIndexVector.size() - 1; idx += 2) {
    unsigned idx0 = _thetaIndexVector[idx].second;
    unsigned idx1 = _thetaIndexVector[idx + 1].second;
    Point<double>& p0 = _points[idx0];
    Point<double>& p1 = _points[idx1];
    file << "plt.scatter(" << p0.getX() << ", " << p0.getY() << ")\n";
    file << "plt.scatter(" << p1.getX() << ", " << p1.getY() << ")\n";
    file << "plt.plot([" << p0.getX() << ", " << p1.getX() << "], ["
         << p0.getY() << ", " << p1.getY() << "])\n";
  }

  file << "plt.show()\n";
  file.close();
}

void SinkClustering::findBestMatching(unsigned groupSize)
{
  // Counts how many clusters are in each solution.
  std::vector<unsigned> clusters(groupSize, 0);
  // Keeps track of the total cost of each solution.
  std::vector<double> costs(groupSize, 0);
  std::vector<double> previousCosts(groupSize, 0);
  // Has the points for each cluster of each solution.
  std::vector<std::vector<std::vector<Point<double>>>> solutionPoints;
  // Has the sink indexes for each cluster of each solution.
  std::vector<std::vector<std::vector<unsigned>>> solutions;

  // Iterates over the theta vector.
  for (unsigned i = 0; i < _thetaIndexVector.size(); ++i) {
    // The - groupSize is because each solution will start on a different index.
    // There is groupSize solutions.
    for (unsigned j = 0; j < groupSize; ++j) {
      if (!((i + j) >= _thetaIndexVector.size())) {
        // Add vectors in case they are no allocated yet.
        if (solutions.size() < (j + 1)) {
          std::vector<std::vector<unsigned>> clusterIndexes;
          solutions.push_back(clusterIndexes);
          std::vector<std::vector<Point<double>>> clusterPoints;
          solutionPoints.push_back(clusterPoints);
        }
        if (solutions[j].size() < (clusters[j] + 1)) {
          std::vector<unsigned> indexesVector;
          solutions[j].push_back(indexesVector);
          std::vector<Point<double>> pointsVector;
          solutionPoints[j].push_back(pointsVector);
        }
        // Get the current point
        unsigned idx = _thetaIndexVector[i + j].second;
        Point<double>& p = _points[idx];
        double distanceCost = 0;
        // Check the distance from the current point to others in the cluster,
        // if there are any.
        for (Point<double> comparisonPoint : solutionPoints[j][clusters[j]]) {
          double cost = p.computeDist(comparisonPoint);
          if (cost > distanceCost) {
            distanceCost = cost;
          }
        }
        // If the cluster size is higher than groupSize,
        // or the distance is higher than _maxInternalDiameter
        //-> start another cluster and save the cost of the current one.
        if (solutionPoints[j][clusters[j]].size() >= groupSize
            || distanceCost > _maxInternalDiameter) {
          // The cost is computed as the highest cost found on the current
          // cluster
          if (previousCosts[j] == 0) {
            previousCosts[j] = _maxInternalDiameter;
          }
          costs[j] += previousCosts[j];
          // A new cluster is defined
          clusters[j] = clusters[j] + 1;
          // The cost was already saved, so the same structure can be used for
          // the next cluster.
          previousCosts[j] = 0;
        } else {
          // Node will be a part of the current cluster, thus, save the highest
          // cost.
          if (distanceCost > previousCosts[j]) {
            previousCosts[j] = distanceCost;
          }
        }
        // Add vectors in case they are no allocated yet. (Depends if a new
        // cluster was defined above)
        if (solutions[j].size() < (clusters[j] + 1)) {
          std::vector<unsigned> indexesVector;
          solutions[j].push_back(indexesVector);
          std::vector<Point<double>> pointsVector;
          solutionPoints[j].push_back(pointsVector);
        }
        // Save the current Point in it's respective cluster. (Depends if a new
        // cluster was defined above)
        solutionPoints[j][clusters[j]].push_back(p);
        solutions[j][clusters[j]].push_back(idx);
      }
    }
  }

  // Same computation as above, however, only for the first groupSize Points.
  for (unsigned i = 0; i < groupSize; ++i) {
    // This is because every solution after the first one skips a Point (starts
    // one late).
    for (unsigned j = (i + 1); j < groupSize; ++j) {
      if (solutions[j].size() < (clusters[j] + 1)) {
        std::vector<unsigned> indexesVector;
        solutions[j].push_back(indexesVector);
        std::vector<Point<double>> pointsVector;
        solutionPoints[j].push_back(pointsVector);
      }
      // Thus here we will assign the Points missing from those solutions.
      unsigned idx = _thetaIndexVector[i].second;
      Point<double>& p = _points[idx];
      double distanceCost = 0;
      for (Point<double> comparisonPoint : solutionPoints[j][clusters[j]]) {
        double cost = p.computeDist(comparisonPoint);
        if (cost > distanceCost) {
          distanceCost = cost;
        }
      }
      if (solutionPoints[j][clusters[j]].size() >= groupSize
          || distanceCost > _maxInternalDiameter) {
        if (previousCosts[j] == 0) {
          previousCosts[j] = _maxInternalDiameter;
        }
        costs[j] += previousCosts[j];
        clusters[j] = clusters[j] + 1;
        previousCosts[j] = 0;
      } else {
        if (distanceCost > previousCosts[j]) {
          previousCosts[j] = distanceCost;
        }
      }
      if (solutions[j].size() < (clusters[j] + 1)) {
        std::vector<unsigned> indexesVector;
        solutions[j].push_back(indexesVector);
        std::vector<Point<double>> pointsVector;
        solutionPoints[j].push_back(pointsVector);
      }
      solutionPoints[j][clusters[j]].push_back(p);
      solutions[j][clusters[j]].push_back(idx);
    }
  }

  unsigned bestSolution = 0;
  double bestSolutionCost = costs[0];

  // Find the solution with minimum cost.
  for (unsigned j = 1; j < groupSize; ++j) {
    if (costs[j] < bestSolutionCost) {
      bestSolution = j;
      bestSolutionCost = costs[j];
    }
  }

  // Save the solution for the Tree Builder.
  _bestSolution = solutions[bestSolution];
}

void SinkClustering::writePlotFile(unsigned groupSize)
{
  std::ofstream file("plot_clustering.py");
  std::ofstream cFile("clusterInfo.csv");
  cFile << "NodeLocX,NodeLocY,NodeCluster,MaxDiameter\n";
  file << "import numpy as np\n";
  file << "import matplotlib.pyplot as plt\n";
  file << "import matplotlib.path as mpath\n";
  file << "import matplotlib.lines as mlines\n";
  file << "import matplotlib.patches as mpatches\n";
  file << "from matplotlib.collections import PatchCollection\n\n";
  std::vector<std::string> colors;
  colors.push_back("tab:blue");
  colors.push_back("tab:orange");
  colors.push_back("tab:green");
  colors.push_back("tab:red");
  colors.push_back("tab:purple");
  colors.push_back("tab:brown");
  colors.push_back("tab:pink");
  colors.push_back("tab:gray");
  colors.push_back("tab:olive");
  colors.push_back("tab:cyan");

  unsigned clusterCounter = 0;
  for (std::vector<unsigned> clusters : _bestSolution) {
    unsigned color = clusterCounter % colors.size();
    for (unsigned idx : clusters) {
      Point<double>& point = _points[idx];
      file << "plt.scatter(" << point.getX() << ", " << point.getY() << ", c=\""
           << colors[color] << "\")\n";
      cFile << point.getX() << "," << point.getY() << "," << clusterCounter
            << "," << _maxInternalDiameter << "\n";
    }
    clusterCounter++;
  }

  file << "plt.show()\n";
  file.close();
  cFile.close();
}

}  // namespace cts

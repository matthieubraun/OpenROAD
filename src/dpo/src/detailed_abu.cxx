///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (c) 2021, Andrew Kennings
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

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Includes.
////////////////////////////////////////////////////////////////////////////////
#include "detailed_abu.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <boost/format.hpp>
#include <boost/tokenizer.hpp>
#include <cmath>
#include <iostream>
#include <stack>
#include <utility>
#include "detailed_orient.h"

////////////////////////////////////////////////////////////////////////////////
// Defines.
////////////////////////////////////////////////////////////////////////////////

#if defined(USE_ISPD14)
#define BIN_AREA_THRESHOLD 0.2
#define FREE_SPACE_THRESHOLD 0.2
#define BIN_DIM 4.0
#define ABU_ALPHA 1.0
#define ABU2_WGT 10
#define ABU5_WGT 4
#define ABU10_WGT 2
#define ABU20_WGT 1
#else
#define BIN_AREA_THRESHOLD 0.2
#define FREE_SPACE_THRESHOLD 0.2
#define BIN_DIM 9.0
#define ABU_ALPHA 1.0
#define ABU2_WGT 10
#define ABU5_WGT 4
#define ABU10_WGT 2
#define ABU20_WGT 1
#endif

namespace dpo {

////////////////////////////////////////////////////////////////////////////////
// Classes.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedABU::DetailedABU(Architecture* arch, Network* network,
                         RoutingParams* rt)
    : DetailedObjective(), m_arch(arch), m_network(network), m_rt(rt) {
  m_name = "abu";
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedABU::~DetailedABU(void) {}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::init(void) {
  // Determine how to size the ABU grid.  We can set according to the detailed
  // placement contest, or we can set according to the GCELL size...  Or, we
  // can do something even different...

  m_abuTargUt = m_mgrPtr->getTargetUt();  // XXX: Need to set this somehow!!!
  std::cout << "Target Utilization for ABU metric is " << m_abuTargUt
            << std::endl;

  m_abuGridUnit = BIN_DIM * m_arch->m_rows[0]->m_rowHeight;
  m_abuGridNumX = (int)ceil((m_arch->m_xmax - m_arch->m_xmin) / m_abuGridUnit);
  m_abuGridNumY = (int)ceil((m_arch->m_ymax - m_arch->m_ymin) / m_abuGridUnit);
  m_abuNumBins = m_abuGridNumX * m_abuGridNumY;
  m_abuBins.resize(m_abuNumBins);

  clearBuckets();
  clearBins();

  // Initialize the density map.  Then, add in the fixed stuff.  We only need to
  // do this once!  During ABU computation (eitheer incremental or from
  // scratch), we only deal with moveable stuff.

  for (int j = 0; j < m_abuGridNumY; j++) {
    for (int k = 0; k < m_abuGridNumX; k++) {
      unsigned binId = j * m_abuGridNumX + k;

      m_abuBins[binId].id = binId;

      m_abuBins[binId].lx = m_arch->m_xmin + k * m_abuGridUnit;
      m_abuBins[binId].ly = m_arch->m_ymin + j * m_abuGridUnit;
      m_abuBins[binId].hx = m_abuBins[binId].lx + m_abuGridUnit;
      m_abuBins[binId].hy = m_abuBins[binId].ly + m_abuGridUnit;

      m_abuBins[binId].hx = std::min(m_abuBins[binId].hx, m_arch->m_xmax);
      m_abuBins[binId].hy = std::min(m_abuBins[binId].hy, m_arch->m_ymax);

      double w = m_abuBins[binId].hx - m_abuBins[binId].lx;
      double h = m_abuBins[binId].hy - m_abuBins[binId].ly;

      m_abuBins[binId].area = std::max(w * h, 0.0);
      m_abuBins[binId].m_util = 0.0;
      m_abuBins[binId].f_util = 0.0;
      m_abuBins[binId].c_util = 0.0;
      m_abuBins[binId].free_space = m_abuBins[binId].area;
    }
  }

  // Insert fixed stuff.
  for (int i = 0; i < m_network->m_nodes.size(); i++) {
    Node* nd = &(m_network->m_nodes[i]);

    if (nd->getType() == NodeType_TERMINAL ||
        nd->getType() == NodeType_TERMINAL_NI) {
      continue;
    }
    if (m_network->m_shapes[nd->getId()].size() != 0) {
      continue;
    }
    if (nd->getFixed() == NodeFixed_NOT_FIXED) {
      continue;
    }

    int lcol = std::max(
        (int)floor(((nd->getX() - 0.5 * nd->getWidth()) - m_arch->m_xmin) /
                   m_abuGridUnit),
        0);
    int rcol = std::min(
        (int)floor(((nd->getX() + 0.5 * nd->getWidth()) - m_arch->m_xmin) /
                   m_abuGridUnit),
        m_abuGridNumX - 1);
    int brow = std::max(
        (int)floor(((nd->getY() - 0.5 * nd->getHeight()) - m_arch->m_ymin) /
                   m_abuGridUnit),
        0);
    int trow = std::min(
        (int)floor(((nd->getY() + 0.5 * nd->getHeight()) - m_arch->m_ymin) /
                   m_abuGridUnit),
        m_abuGridNumY - 1);

    for (int j = brow; j <= trow; j++) {
      for (int k = lcol; k <= rcol; k++) {
        unsigned binId = j * m_abuGridNumX + k;

        // Get intersection
        double lx =
            std::max(m_abuBins[binId].lx, nd->getX() - 0.5 * nd->getWidth());
        double hx =
            std::min(m_abuBins[binId].hx, nd->getX() + 0.5 * nd->getWidth());
        double ly =
            std::max(m_abuBins[binId].ly, nd->getY() - 0.5 * nd->getHeight());
        double hy =
            std::min(m_abuBins[binId].hy, nd->getY() + 0.5 * nd->getHeight());

        if ((hx - lx) > 1.0e-5 && (hy - ly) > 1.0e-5) {
          double common_area = (hx - lx) * (hy - ly);
          if (nd->getFixed() != NodeFixed_NOT_FIXED) {
            m_abuBins[binId].f_util += common_area;
          }
        }
      }
    }
  }

  // Insert shapes (which are also fixed).
  for (int i = 0; i < m_network->m_nodes.size(); i++) {
    Node* nd = &(m_network->m_nodes[i]);

    if (m_network->m_shapes[nd->getId()].size() == 0) {
      // No shapes...
      continue;
    }
    for (int m = 0; m < m_network->m_shapes[nd->getId()].size(); m++) {
      Node* shape = m_network->m_shapes[nd->getId()][m];

      int lcol =
          std::max((int)floor(((shape->getX() - 0.5 * shape->getWidth()) -
                               m_arch->m_xmin) /
                              m_abuGridUnit),
                   0);
      int rcol =
          std::min((int)floor(((shape->getX() + 0.5 * shape->getWidth()) -
                               m_arch->m_xmin) /
                              m_abuGridUnit),
                   m_abuGridNumX - 1);
      int brow =
          std::max((int)floor(((shape->getY() - 0.5 * shape->getHeight()) -
                               m_arch->m_ymin) /
                              m_abuGridUnit),
                   0);
      int trow =
          std::min((int)floor(((shape->getY() + 0.5 * shape->getHeight()) -
                               m_arch->m_ymin) /
                              m_abuGridUnit),
                   m_abuGridNumY - 1);

      for (int j = brow; j <= trow; j++) {
        for (int k = lcol; k <= rcol; k++) {
          unsigned binId = j * m_abuGridNumX + k;

          /* get intersection */
          double lx = std::max(m_abuBins[binId].lx,
                               shape->getX() - 0.5 * shape->getWidth());
          double hx = std::min(m_abuBins[binId].hx,
                               shape->getX() + 0.5 * shape->getWidth());
          double ly = std::max(m_abuBins[binId].ly,
                               shape->getY() - 0.5 * shape->getHeight());
          double hy = std::min(m_abuBins[binId].hy,
                               shape->getY() + 0.5 * shape->getHeight());

          if ((hx - lx) > 1.0e-5 && (hy - ly) > 1.0e-5) {
            double common_area = (hx - lx) * (hy - ly);
            m_abuBins[binId].f_util += common_area;
          }
        }
      }
    }
  }

  // Row blockages (filler nodes).
  for (int i = 0; i < m_network->m_filler.size(); i++) {
    Node* nd = m_network->m_filler[i];

    int lcol = std::max(
        (int)floor(((nd->getX() - 0.5 * nd->getWidth()) - m_arch->m_xmin) /
                   m_abuGridUnit),
        0);
    int rcol = std::min(
        (int)floor(((nd->getX() + 0.5 * nd->getWidth()) - m_arch->m_xmin) /
                   m_abuGridUnit),
        m_abuGridNumX - 1);
    int brow = std::max(
        (int)floor(((nd->getY() - 0.5 * nd->getHeight()) - m_arch->m_ymin) /
                   m_abuGridUnit),
        0);
    int trow = std::min(
        (int)floor(((nd->getY() + 0.5 * nd->getHeight()) - m_arch->m_ymin) /
                   m_abuGridUnit),
        m_abuGridNumY - 1);

    for (int j = brow; j <= trow; j++) {
      for (int k = lcol; k <= rcol; k++) {
        unsigned binId = j * m_abuGridNumX + k;

        /* get intersection */
        double lx =
            std::max(m_abuBins[binId].lx, nd->getX() - 0.5 * nd->getWidth());
        double hx =
            std::min(m_abuBins[binId].hx, nd->getX() + 0.5 * nd->getWidth());
        double ly =
            std::max(m_abuBins[binId].ly, nd->getY() - 0.5 * nd->getHeight());
        double hy =
            std::min(m_abuBins[binId].hy, nd->getY() + 0.5 * nd->getHeight());

        if ((hx - lx) > 1.0e-5 && (hy - ly) > 1.0e-5) {
          double common_area = (hx - lx) * (hy - ly);
          m_abuBins[binId].f_util += common_area;
        }
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::init(DetailedMgr* mgrPtr, DetailedOrient* orientPtr) {
  m_orientPtr = orientPtr;
  m_mgrPtr = mgrPtr;
  init();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::clearUtils(void) {
  // Set utilizations to zero.
  for (int j = 0; j < m_abuGridNumY; j++) {
    for (int k = 0; k < m_abuGridNumX; k++) {
      unsigned binId = j * m_abuGridNumX + k;

      m_abuBins[binId].m_util = 0.0;
      m_abuBins[binId].c_util = 0.0;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::computeUtils(void) {
  // Insert movables.
  for (int i = 0; i < m_network->m_nodes.size(); i++) {
    Node* nd = &(m_network->m_nodes[i]);

    if (nd->getType() == NodeType_TERMINAL ||
        nd->getType() == NodeType_TERMINAL_NI) {
      continue;
    }
    if (nd->getFixed() != NodeFixed_NOT_FIXED) {
      continue;
    }
    if (m_network->m_shapes[nd->getId()].size() != 0) {
      continue;
    }

    double nlx = nd->getX() - 0.5 * nd->getWidth();
    double nrx = nd->getX() + 0.5 * nd->getWidth();
    double nly = nd->getY() - 0.5 * nd->getHeight();
    double nhy = nd->getY() + 0.5 * nd->getHeight();

    int lcol = std::max((int)floor((nlx - m_arch->m_xmin) / m_abuGridUnit), 0);
    int rcol = std::min((int)floor((nrx - m_arch->m_xmin) / m_abuGridUnit),
                        m_abuGridNumX - 1);
    int brow = std::max((int)floor((nly - m_arch->m_ymin) / m_abuGridUnit), 0);
    int trow = std::min((int)floor((nhy - m_arch->m_ymin) / m_abuGridUnit),
                        m_abuGridNumY - 1);

    // Cell area...
    for (int j = brow; j <= trow; j++) {
      for (int k = lcol; k <= rcol; k++) {
        unsigned binId = j * m_abuGridNumX + k;

        // get intersection
        double lx = std::max(m_abuBins[binId].lx, nlx);
        double hx = std::min(m_abuBins[binId].hx, nrx);
        double ly = std::max(m_abuBins[binId].ly, nly);
        double hy = std::min(m_abuBins[binId].hy, nhy);

        if ((hx - lx) > 1.0e-5 && (hy - ly) > 1.0e-5) {
          double common_area = (hx - lx) * (hy - ly);
          m_abuBins[binId].m_util += common_area;
        }
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::computeBuckets(void) {
  // Put bins into buckets.
  for (size_t i = 0; i < m_utilBuckets.size(); i++) {
    m_utilBuckets[i].erase(m_utilBuckets[i].begin(), m_utilBuckets[i].end());
  }
  double denom = 1. / (double)m_utilBuckets.size();
  for (int j = 0; j < m_abuGridNumY; j++) {
    for (int k = 0; k < m_abuGridNumX; k++) {
      unsigned binId = j * m_abuGridNumX + k;

      int ix = getBucketId(binId, m_abuBins[binId].m_util);
      if (ix != -1) {
        m_utilBuckets[ix].insert(binId);
      }
    }
  }
  for (size_t i = 0; i < m_utilBuckets.size(); i++) {
    m_utilTotals[i] = 0.;
    for (std::set<int>::iterator it = m_utilBuckets[i].begin();
         it != m_utilBuckets[i].end(); it++) {
      double space = m_abuBins[*it].area - m_abuBins[*it].f_util;
      double util = m_abuBins[*it].m_util;

      m_utilTotals[i] += util / space;
    }
  }

  // for( size_t i = 0; i < m_utilBuckets.size(); i++ )
  //{
  //    int n = std::max( (int)m_utilBuckets[i].size(), 1 );
  //    double avg = m_utilTotals[i] / (double)n;
  //    std::cout << boost::format( "[%.2lf,%.2lf]: %6d %10.4lf %10.4lf" )
  //        % (i*denom)
  //        % ((i+1)*denom)
  //        % m_utilBuckets[i].size()
  //        % m_utilTotals[i]
  //        % avg
  //        << std::endl;
  //}
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
int DetailedABU::getBucketId(int binId, double occ) {
  // Update the utilization buckets based on the current utilization.

  if (m_abuBins[binId].area <=
      m_abuGridUnit * m_abuGridUnit * BIN_AREA_THRESHOLD) {
    return -1;
  }
  double free_space = m_abuBins[binId].area - m_abuBins[binId].f_util;
  if (free_space <= FREE_SPACE_THRESHOLD * m_abuBins[binId].area) {
    return -1;
  }

  double util = occ / free_space;

  double denom = 1. / (double)m_utilBuckets.size();
  int ix =
      std::max(0, std::min((int)(util / denom), (int)m_utilBuckets.size() - 1));

  return ix;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::measureABU(bool print) {
  clearUtils();
  computeUtils();
  clearBuckets();
  computeBuckets();

  return calculateABU(print);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::calculateABU(bool print) {
  // Computes the ABU using the bin's current utilizations.  So, could be wrong
  // if bins not kept up-to-date.

  m_abuTargUt02 = 0.0;
  m_abuTargUt05 = 0.0;
  m_abuTargUt10 = 0.0;
  m_abuTargUt20 = 0.0;

  std::vector<double> util_array(m_abuNumBins, 0.0);
  /* 2. determine the free space & utilization per bin */
  for (int j = 0; j < m_abuGridNumY; j++) {
    for (int k = 0; k < m_abuGridNumX; k++) {
      unsigned binId = j * m_abuGridNumX + k;
      if (m_abuBins[binId].area >
          m_abuGridUnit * m_abuGridUnit * BIN_AREA_THRESHOLD) {
        m_abuBins[binId].free_space =
            m_abuBins[binId].area - m_abuBins[binId].f_util;
        if (m_abuBins[binId].free_space >
            FREE_SPACE_THRESHOLD * m_abuBins[binId].area) {
          util_array[binId] =
              m_abuBins[binId].m_util / m_abuBins[binId].free_space;
        }
      }
    }
  }

  std::sort(util_array.begin(), util_array.end());

  /* 3. obtain ABU numbers */
  double abu1 = 0.0, abu2 = 0.0, abu5 = 0.0, abu10 = 0.0, abu20 = 0.0;
  int clip_index = 0.01 * m_abuNumBins;
  for (int j = m_abuNumBins - 1; j > m_abuNumBins - 1 - clip_index; j--) {
    abu1 += util_array[j];
  }
  abu1 = (clip_index) ? abu1 / clip_index : util_array[m_abuNumBins - 1];

  clip_index = 0.02 * m_abuNumBins;
  for (int j = m_abuNumBins - 1; j > m_abuNumBins - 1 - clip_index; j--) {
    abu2 += util_array[j];
  }
  abu2 = (clip_index) ? abu2 / clip_index : util_array[m_abuNumBins - 1];

  clip_index = 0.05 * m_abuNumBins;
  for (int j = m_abuNumBins - 1; j > m_abuNumBins - 1 - clip_index; j--) {
    abu5 += util_array[j];
  }
  abu5 = (clip_index) ? abu5 / clip_index : util_array[m_abuNumBins - 1];

  clip_index = 0.10 * m_abuNumBins;
  for (int j = m_abuNumBins - 1; j > m_abuNumBins - 1 - clip_index; j--) {
    abu10 += util_array[j];
  }
  abu10 = (clip_index) ? abu10 / clip_index : util_array[m_abuNumBins - 1];

  clip_index = 0.20 * m_abuNumBins;
  for (int j = m_abuNumBins - 1; j > m_abuNumBins - 1 - clip_index; j--) {
    abu20 += util_array[j];
  }
  abu20 = (clip_index) ? abu20 / clip_index : util_array[m_abuNumBins - 1];
  util_array.clear();

  if (print) {
    std::cout << "  target util       : " << m_abuTargUt << std::endl;
    std::cout << "  AVG_2,5,10,20     : " << abu2 << ", " << abu5 << ", "
              << abu10 << ", " << abu20 << std::endl;
  }

  m_abuTargUt02 = abu2;
  m_abuTargUt05 = abu5;
  m_abuTargUt10 = abu10;
  m_abuTargUt20 = abu20;

  /* calculate overflow and penalty */
  if (std::fabs(m_abuTargUt - 1.0) <= 1.0e-3) {
    abu1 = abu2 = abu5 = abu10 = abu20 = 0.0;
  } else {
    abu1 = std::max(0.0, abu1 / m_abuTargUt - 1.0);
    abu2 = std::max(0.0, abu2 / m_abuTargUt - 1.0);
    abu5 = std::max(0.0, abu5 / m_abuTargUt - 1.0);
    abu10 = std::max(0.0, abu10 / m_abuTargUt - 1.0);
    abu20 = std::max(0.0, abu20 / m_abuTargUt - 1.0);
  }

  double penalty = (ABU2_WGT * abu2 + ABU5_WGT * abu5 + ABU10_WGT * abu10 +
                    ABU20_WGT * abu20) /
                   (double)(ABU2_WGT + ABU5_WGT + ABU10_WGT + ABU20_WGT);

  if (print) {
    std::cout << "  ABU_2,5,10,20     : " << abu2 << ", " << abu5 << ", "
              << abu10 << ", " << abu20 << std::endl;
    std::cout << "  penalty           : " << penalty << std::endl;
  }

  return penalty;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::curr() {
  if (m_abuTargUt >= 0.999) return 0.;

  clearUtils();
  computeUtils();
  clearBuckets();
  computeBuckets();

  // Create a cost based on the buckets.  Something based on the buckets
  // which is "normalized" towards 0 when nothing is wrong.  This
  // implies we can use some sort of (1.0+penalty) during annealing.
  int n = (int)m_utilBuckets.size();
  double denom = 1.0 / (double)n;
  double fof = 0.;
  for (int i = n; (i * denom) > m_abuTargUt;) {
    --i;
    if (m_utilBuckets[i].size() != 0) {
      fof += m_utilTotals[i] / m_utilBuckets[i].size();
    }
  }
  // std::cout << "ABU: fof is " << fof << std::endl;
  return fof;

  // The following calculation is _not_ any sort of normalized number
  // which can be bad...
  /*
  double util = 0.;
  double pen = 0.;
  for(int j=0;j<m_abuGridNumY;j++)
  {
      for(int k=0;k<m_abuGridNumX;k++)
      {
          int binId= j*m_abuGridNumX+k;
          if(m_abuBins[binId].area >
  m_abuGridUnit*m_abuGridUnit*BIN_AREA_THRESHOLD)
          {
              double free_space = m_abuBins[binId].area-m_abuBins[binId].f_util;
              if(free_space > FREE_SPACE_THRESHOLD*m_abuBins[binId].area)
              {
                  util = m_abuBins[binId].m_util / free_space;
                  pen += (m_abuTargUt - std::max(util,
  m_abuTargUt))/(m_abuTargUt - 1.);
              }
          }
      }
  }
  std::cout << "ABU: curr is " << pen << std::endl;
  return pen;
  */
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::delta(int n, std::vector<Node*>& nodes,
                          std::vector<double>& curX, std::vector<double>& curY,
                          std::vector<unsigned>& curOri,
                          std::vector<double>& newX, std::vector<double>& newY,
                          std::vector<unsigned>& newOri) {
  // Need change in fof metric.  Not many bins involved, so should be
  // fast to compute old and new.
  double denom = 1.0 / (double)m_utilBuckets.size();

  double fofOld = 0.;
  for (int i = m_utilBuckets.size(); (i * denom) > m_abuTargUt;) {
    --i;
    if (m_utilBuckets[i].size() != 0) {
      fofOld += m_utilTotals[i] / m_utilBuckets[i].size();
    }
  }
  // std::cout << "ABU: fof is " << fof << std::endl;

  // Compute changed bins and changed occupancy.
  ++m_abuChangedBinsCounter;
  for (int i = 0; i < n; i++) {
    updateBins(nodes[i], curX[i], curY[i], -1);
  }
  for (int i = 0; i < n; i++) {
    updateBins(nodes[i], newX[i], newY[i], +1);
  }

  double space = 0.;
  double util = 0.;
  int ix = -1;
  std::set<int>::iterator it;
  for (int i = 0; i < m_abuChangedBins.size(); i++) {
    int binId = m_abuChangedBins[i];

    space = m_abuBins[binId].area - m_abuBins[binId].f_util;

    util = m_abuBins[binId].c_util;
    if ((ix = getBucketId(binId, util)) != -1) {
      if (m_utilBuckets[ix].end() == (it = m_utilBuckets[ix].find(binId))) {
        std::cout << "Error." << std::endl;
        exit(-1);
      }
      m_utilBuckets[ix].erase(it);
      m_utilTotals[ix] -= util / space;
    }

    util = m_abuBins[binId].m_util;
    if ((ix = getBucketId(binId, util)) != -1) {
      m_utilBuckets[ix].insert(binId);
      m_utilTotals[ix] += util / space;
    }
  }

  double fofNew = 0.;
  for (int i = m_utilBuckets.size(); (i * denom) > m_abuTargUt;) {
    --i;
    if (m_utilBuckets[i].size() != 0) {
      fofNew += m_utilTotals[i] / m_utilBuckets[i].size();
    }
  }
  double fofDelta = fofOld - fofNew;
  return fofDelta;

  // The following is not any sort of normalized number which can
  // be bad...
  /*
  double delta_of = delta();
  return delta_of;
  */
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::delta(void) {
  if (m_abuTargUt >= 0.999) return 0.0;

  double util_0 = 0., util_1 = 0.;
  double pen_0 = 0., pen_1 = 0.;
  double delta = 0.;
  double sum_0 = 0.;
  double sum_1 = 0.;
  for (int i = 0; i < m_abuChangedBins.size(); i++) {
    int binId = m_abuChangedBins[i];
    if (m_abuBins[binId].area >
        m_abuGridUnit * m_abuGridUnit * BIN_AREA_THRESHOLD) {
      double free_space = m_abuBins[binId].area - m_abuBins[binId].f_util;
      if (free_space > FREE_SPACE_THRESHOLD * m_abuBins[binId].area) {
        util_0 = m_abuBins[binId].c_util / free_space;
        pen_0 =
            (m_abuTargUt - std::max(util_0, m_abuTargUt)) / (m_abuTargUt - 1.);
        sum_0 += pen_0;

        util_1 = m_abuBins[binId].m_util / free_space;
        pen_1 =
            (m_abuTargUt - std::max(util_1, m_abuTargUt)) / (m_abuTargUt - 1.);
        sum_1 += pen_1;

        // delta += pen_0;
        // delta -= pen_1;
      }
    }
  }
  delta = sum_0 - sum_1;

  // XXX: A +ve value returned means improvement...
  return delta;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::updateBins(Node* nd, double x, double y, int addSub) {
  // Updates the bins incrementally.  Can add or remove a *MOVABLE* node's
  // contribution to the bin utilization.  Assumes the node is located at (x,y)
  // rather than the position stored in the node...

  if (nd->getType() == NodeType_TERMINAL ||
      nd->getType() == NodeType_TERMINAL_NI) {
    std::cout << "Error." << std::endl;
    exit(-1);
  }
  if (nd->getFixed() != NodeFixed_NOT_FIXED) {
    std::cout << "Error." << std::endl;
    exit(-1);
  }
  if (m_network->m_shapes[nd->getId()].size() != 0) {
    std::cout << "Error." << std::endl;
    exit(-1);
  }

  int lcol = std::max(
      (int)floor(((x - 0.5 * nd->getWidth()) - m_arch->m_xmin) / m_abuGridUnit),
      0);
  int rcol = std::min(
      (int)floor(((x + 0.5 * nd->getWidth()) - m_arch->m_xmin) / m_abuGridUnit),
      m_abuGridNumX - 1);
  int brow =
      std::max((int)floor(((y - 0.5 * nd->getHeight()) - m_arch->m_ymin) /
                          m_abuGridUnit),
               0);
  int trow =
      std::min((int)floor(((y + 0.5 * nd->getHeight()) - m_arch->m_ymin) /
                          m_abuGridUnit),
               m_abuGridNumY - 1);

  for (int j = brow; j <= trow; j++) {
    for (int k = lcol; k <= rcol; k++) {
      unsigned binId = j * m_abuGridNumX + k;

      // get intersection
      double lx = std::max(m_abuBins[binId].lx, x - 0.5 * nd->getWidth());
      double hx = std::min(m_abuBins[binId].hx, x + 0.5 * nd->getWidth());
      double ly = std::max(m_abuBins[binId].ly, y - 0.5 * nd->getHeight());
      double hy = std::min(m_abuBins[binId].hy, y + 0.5 * nd->getHeight());

      if ((hx - lx) > 1.0e-5 && (hy - ly) > 1.0e-5) {
        // XXX: Keep track of the bins that change.
        if (m_abuChangedBinsMask[binId] != m_abuChangedBinsCounter) {
          m_abuChangedBinsMask[binId] = m_abuChangedBinsCounter;
          m_abuChangedBins.push_back(binId);

          // Record original usage the first time we observe that this bin is
          // going to change usage.
          m_abuBins[binId].c_util = m_abuBins[binId].m_util;
        }
        double common_area = (hx - lx) * (hy - ly);
        m_abuBins[binId].m_util += common_area * (double)addSub;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::acceptBins(void) {
  m_abuChangedBins.erase(m_abuChangedBins.begin(), m_abuChangedBins.end());
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::rejectBins(void) {
  double space = 0.;
  double util = 0.;
  int ix = -1;
  std::set<int>::iterator it;
  for (int i = 0; i < m_abuChangedBins.size(); i++) {
    int binId = m_abuChangedBins[i];
    space = m_abuBins[binId].area - m_abuBins[binId].f_util;

    // Remove from current bucket.
    util = m_abuBins[binId].m_util;
    if ((ix = getBucketId(binId, util)) != -1) {
      if (m_utilBuckets[ix].end() == (it = m_utilBuckets[ix].find(binId))) {
        std::cout << "Error." << std::endl;
        exit(-1);
      }
      m_utilBuckets[ix].erase(it);
      m_utilTotals[ix] -= util / space;
    }

    // Insert into original bucket.
    util = m_abuBins[binId].c_util;
    if ((ix = getBucketId(binId, util)) != -1) {
      m_utilBuckets[ix].insert(binId);
      m_utilTotals[ix] += util / space;
    }

    // Restore original utilization.
    m_abuBins[binId].m_util = m_abuBins[binId].c_util;
  }

  m_abuChangedBins.erase(m_abuChangedBins.begin(), m_abuChangedBins.end());
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::clearBins(void) {
  m_abuChangedBinsCounter = 0;
  m_abuChangedBinsMask.resize(m_abuNumBins);
  std::fill(m_abuChangedBinsMask.begin(), m_abuChangedBinsMask.end(),
            m_abuChangedBinsCounter);
  ++m_abuChangedBinsCounter;
  m_abuChangedBins.reserve(m_abuNumBins);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::clearBuckets(void) {
  m_utilBuckets.resize(10);
  m_utilTotals.resize(10);
  std::fill(m_utilTotals.begin(), m_utilTotals.end(), 0.0);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::accept(void) { acceptBins(); }

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedABU::reject(void) { rejectBins(); }

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::freeSpaceThreshold(void) { return FREE_SPACE_THRESHOLD; }

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::binAreaThreshold(void) { return BIN_AREA_THRESHOLD; }

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedABU::alpha(void) { return ABU_ALPHA; }

}  // namespace dpo

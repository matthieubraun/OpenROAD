///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (c) 2019, OpenROAD
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

#pragma once

#include <QFrame>
#include <QMainWindow>
#include <QOpenGLWidget>
#include <QScrollArea>
#include <QShortcut>
#include <map>
#include <vector>

#include "gui/gui.h"
#include "opendb/dbBlockCallBackObj.h"
#include "options.h"
#include "search.h"

namespace odb {
class dbBlock;
class dbDatabase;
class dbInst;
class dbMaster;
class dbTransform;
class dbTechLayer;
}  // namespace odb

namespace gui {

class LayoutScroll;

// This class draws the layout.  It supports:
//   * zoom in/out with ctrl-mousewheel
//   * rubber band zoom with right mouse button
//   * fit with 'F' key
// The display follows the display options for visibility.
//
// This object resizes with zooming but only the visible
// portion of this widget is ever drawn.
class LayoutViewer : public QWidget, public odb::dbBlockCallBackObj
{
  Q_OBJECT

 public:
  LayoutViewer(Options* options,
               const SelectionSet& selected,
               const SelectionSet& highlighted,
               QWidget* parent = nullptr);

  void setDb(odb::dbDatabase* db);
  qreal getPixelsPerDBU() { return pixelsPerDBU_; }
  void setScroller(LayoutScroll* scroller);

  // From QWidget
  virtual void paintEvent(QPaintEvent* event) override;
  virtual void resizeEvent(QResizeEvent* event) override;
  virtual void mousePressEvent(QMouseEvent* event) override;
  virtual void mouseMoveEvent(QMouseEvent* event) override;
  virtual void mouseReleaseEvent(QMouseEvent* event) override;

  // From dbBlockCallBackObj
  virtual void inDbPostMoveInst(odb::dbInst* inst) override;
  virtual void inDbFillCreate(odb::dbFill* fill) override;

 signals:
  void location(qreal x, qreal y);
  void selected(const Selected& selected);
  void addSelected(const Selected& selected);

 public slots:
  void zoomIn();
  void zoomOut();
  void zoomTo(const odb::Rect& rect_dbu);
  void designLoaded(odb::dbBlock* block);
  void fit();  // fit the whole design in the window

 private:
  struct Boxes
  {
    std::vector<QRect> obs;
    std::vector<QRect> mterms;
  };
  using LayerBoxes = std::map<odb::dbTechLayer*, Boxes>;
  using CellBoxes = std::map<odb::dbMaster*, LayerBoxes>;

  void boxesByLayer(odb::dbMaster* master, LayerBoxes& boxes);
  const Boxes* boxesByLayer(odb::dbMaster* master, odb::dbTechLayer* layer);
  odb::dbBlock* getBlock();
  void setPixelsPerDBU(qreal pixels_per_dbu);
  void drawBlock(QPainter* painter,
                 const odb::Rect& bounds,
                 odb::dbBlock* block,
                 int depth);
  void addInstTransform(QTransform& xfm, const odb::dbTransform& inst_xfm);
  QColor getColor(odb::dbTechLayer* layer);
  Qt::BrushStyle getPattern(odb::dbTechLayer* layer);
  void updateRubberBandRegion();
  void drawTracks(odb::dbTechLayer* layer,
                  odb::dbBlock* block,
                  QPainter* painter,
                  const odb::Rect& bounds);

  void drawRows(odb::dbBlock* block,
                QPainter* painter,
                const odb::Rect& bounds);
  void drawSelected(Painter& painter);
  void drawHighlighted(Painter& painter);
  Selected selectAtPoint(odb::Point pt_dbu);

  odb::Rect screenToDBU(const QRect& rect);
  odb::Point screenToDBU(const QPoint& point);
  QRectF dbuToScreen(const odb::Rect& dbu_rect);

  odb::dbDatabase* db_;
  Options* options_;
  const SelectionSet& selected_;
  const SelectionSet& highlighted_;
  LayoutScroll* scroller_;
  qreal pixels_per_dbu_;
  int min_depth_;
  int max_depth_;
  Search search_;
  bool search_init_;
  CellBoxes cell_boxes_;
  QRect rubber_band_;  // screen coordinates
  bool rubber_band_showing_;
};

// The LayoutViewer widget can become quite large as you zoom
// in so it is stored in a scroll area.
class LayoutScroll : public QScrollArea
{
  Q_OBJECT
 public:
  LayoutScroll(LayoutViewer* viewer, QWidget* parent = 0);

 public slots:
  void zoomIn();
  void zoomOut();

 protected:
  void wheelEvent(QWheelEvent* event) override;

 private:
  LayoutViewer* viewer_;
};

}  // namespace gui

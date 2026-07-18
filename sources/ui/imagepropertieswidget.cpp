/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "imagepropertieswidget.h"

#include "../QPropertyUndoCommand/qpropertyundocommand.h"
#include "../diagram.h"
#include "../qetgraphicsitem/diagramimageitem.h"
#include "../ui_imagepropertieswidget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
/**
	剪裁預覽:等比例縮放顯示圖片,滑鼠拖曳畫出要「保留」的矩形。
	cropRect() 回傳對應到原圖像素座標的矩形。
*/
class CropView : public QWidget
{
	public:
		explicit CropView(const QPixmap &pix, QWidget *parent = nullptr) :
			QWidget(parent), m_pix(pix)
		{
			setMinimumSize(360, 260);
		}

		QRect cropRect() const
		{
			if (m_sel.isNull() || m_scaled.width() < 1
			    || m_scaled.height() < 1)
				return QRect();
			const QRectF sel = QRectF(m_sel).intersected(m_scaled);
			const double sx = double(m_pix.width())  / m_scaled.width();
			const double sy = double(m_pix.height()) / m_scaled.height();
			QRect r(qRound((sel.x() - m_scaled.x()) * sx),
				qRound((sel.y() - m_scaled.y()) * sy),
				qRound(sel.width()  * sx),
				qRound(sel.height() * sy));
			return r.intersected(m_pix.rect());
		}

	protected:
		void paintEvent(QPaintEvent *) override
		{
			QPainter p(this);
			p.fillRect(rect(), palette().window());
			QSize sz = m_pix.size();
			sz.scale(size(), Qt::KeepAspectRatio);
			m_scaled = QRectF(QPointF(0, 0), QSizeF(sz));
			m_scaled.moveCenter(QRectF(rect()).center());
			p.drawPixmap(m_scaled.toRect(), m_pix);
			if (!m_sel.isNull()) {
				// 選取框外變暗
				QPainterPath path;
				path.addRect(m_scaled);
				path.addRect(QRectF(m_sel).intersected(m_scaled));
				p.fillPath(path, QColor(0, 0, 0, 90));
				p.setPen(QPen(Qt::red, 1, Qt::DashLine));
				p.drawRect(m_sel);
			}
		}
		void mousePressEvent(QMouseEvent *e) override
		{
			m_origin = e->pos();
			m_sel = QRect(m_origin, QSize());
			update();
		}
		void mouseMoveEvent(QMouseEvent *e) override
		{
			m_sel = QRect(m_origin, e->pos()).normalized();
			update();
		}

	private:
		QPixmap m_pix;
		QRectF m_scaled;
		QPoint m_origin;
		QRect m_sel;
};
}   // namespace

/**
	@brief ImagePropertiesWidget::ImagePropertiesWidget
	Constructor
	@param image : image to edit properties
	@param parent : parent widget
*/
ImagePropertiesWidget::ImagePropertiesWidget(DiagramImageItem *image, QWidget *parent) :
	PropertiesEditorWidget(parent),
	ui(new Ui::ImagePropertiesWidget),
	m_image(nullptr)
{
	ui->setupUi(this);

	// 剪裁按鈕:開啟預覽,拖曳矩形選取要保留的區域
	auto *crop_btn = new QPushButton(tr("剪裁…"), this);
	ui->gridLayout->addWidget(crop_btn, ui->gridLayout->rowCount(), 0, 1,
				  ui->gridLayout->columnCount());
	connect(crop_btn, &QPushButton::clicked, this, [this]() {
		if (!m_image) return;
		const QPixmap pix = m_image->pixmap();
		if (pix.isNull()) return;
		QDialog dlg(this);
		dlg.setWindowTitle(tr("剪裁圖片"));
		dlg.resize(560, 480);
		auto *lay = new QVBoxLayout(&dlg);
		lay->addWidget(new QLabel(
			tr("在圖片上拖曳出要保留的範圍："), &dlg));
		auto *view = new CropView(pix, &dlg);
		lay->addWidget(view, 1);
		auto *bb = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
		lay->addWidget(bb);
		connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
		connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
		if (dlg.exec() != QDialog::Accepted) return;
		const QRect r = view->cropRect();
		if (r.width() < 2 || r.height() < 2) return;   // 無有效選取
		const QPixmap cropped = pix.copy(r);
		if (m_image->diagram()) {
			auto *undo = new QPropertyUndoCommand(
				m_image, "pixmap", QVariant(pix), QVariant(cropped));
			undo->setText(tr("剪裁圖片"));
			m_image->diagram()->undoStack().push(undo);
		} else {
			m_image->setPixmap(cropped);
		}
	});

	this->setDisabled(true);
	setImageItem(image);
}

/**
	@brief ImagePropertiesWidget::~ImagePropertiesWidget
	Destructor
*/
ImagePropertiesWidget::~ImagePropertiesWidget()
{
	delete ui;
}

/**
	@brief ImagePropertiesWidget::setImageItem
	Set the image to edit properties
	@param image : image to edit
*/
void ImagePropertiesWidget::setImageItem(DiagramImageItem *image)
{
	if(!image) return;
	this->setEnabled(true);
	if (m_image == image) return;
	if (m_image)
		disconnect(m_image, &QGraphicsObject::scaleChanged, this, &ImagePropertiesWidget::updateUi);

	m_image = image;
	connect(m_image, &QGraphicsObject::scaleChanged, this, &ImagePropertiesWidget::updateUi);
	m_movable = image->isMovable();
	m_scale = m_image->scale();
	updateUi();
}

/**
	@brief ImagePropertiesWidget::apply
	Apply the change
*/
void ImagePropertiesWidget::apply()
{
	if(!m_image) return;

	if (m_image->diagram())
	{
		if (m_live_edit) disconnect(m_image, &QGraphicsObject::scaleChanged, this, &ImagePropertiesWidget::updateUi);

		QUndoCommand *undo = associatedUndo();
		if (undo)
			m_image->diagram()->undoStack().push(undo);

		if (m_live_edit) connect(m_image, &QGraphicsObject::scaleChanged, this, &ImagePropertiesWidget::updateUi);
	}

	m_scale = m_image->scale();
}

/**
	@brief ImagePropertiesWidget::reset
	Reset the change
*/
void ImagePropertiesWidget::reset()
{
	if(!m_image) return;

	m_image->setScale(m_scale);
	m_image->setMovable(m_movable);
	updateUi();
}

/**
	@brief ImagePropertiesWidget::setLiveEdit
	@param live_edit true -> enable live edit
 *					false -> disable live edit
	@return always true
*/
bool ImagePropertiesWidget::setLiveEdit(bool live_edit)
{
	if (m_live_edit == live_edit) return true;
	m_live_edit = live_edit;

	if (m_live_edit)
	{
		connect (ui->m_scale_slider, &QSlider::sliderReleased, this, &ImagePropertiesWidget::apply);
		connect (ui->m_scale_sb, &QSpinBox::editingFinished, this, &ImagePropertiesWidget::apply);
	}
	else
	{
		disconnect (ui->m_scale_slider, &QSlider::sliderReleased, this, &ImagePropertiesWidget::apply);
		disconnect (ui->m_scale_sb, &QSpinBox::editingFinished, this, &ImagePropertiesWidget::apply);
	}

	return true;
}

/**
	@brief ImagePropertiesWidget::associatedUndo
	@return the change in an undo command (ItemResizerCommand).
	If there is no change return nullptr
*/
QUndoCommand* ImagePropertiesWidget::associatedUndo() const
{

	qreal value = ui->m_scale_slider->value();
	value /= 100;
	if (m_scale == value) return nullptr;
	QPropertyUndoCommand *undo = new QPropertyUndoCommand(m_image, "scale", m_scale, value);
	undo->enableAnimation();
	undo->setText(tr("Modifier la taille d'une image"));
	return undo;
}

/**
	@brief ImagePropertiesWidget::updateUi
	Udpdate the ui, notably when the image to edit change
*/
void ImagePropertiesWidget::updateUi()
{
	if (!m_image) return;
	ui->m_scale_slider->setValue(m_image->scale() * 100);
	ui->m_lock_pos_cb->setChecked(!m_image->isMovable());
}

/**
	@brief ImagePropertiesWidget::on_m_scale_slider_valueChanged
	Update the size of image when move slider.
	@param value
*/
void ImagePropertiesWidget::on_m_scale_slider_valueChanged(int value)
{
		qreal scale = value;
		m_image->setScale(scale / 100);
}

/**
	@brief ImagePropertiesWidget::on_m_lock_pos_cb_clicked
	Set movable or not the image according to corresponding check box
*/
void ImagePropertiesWidget::on_m_lock_pos_cb_clicked()
{
	m_image->setMovable(!ui->m_lock_pos_cb->isChecked());
}

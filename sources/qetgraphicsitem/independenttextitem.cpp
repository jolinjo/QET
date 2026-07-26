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
#include "independenttextitem.h"

#include "../diagram.h"
#include "../diagramcommands.h"
#include "../qet.h"
#include "../qetapp.h"

#include <QDomElement>
#include <QSettings>
#include <QTextDocument>

/**
	Constructeur
	@param parent_diagram Le schema auquel est rattache le champ de texte
*/
IndependentTextItem::IndependentTextItem() :
	DiagramTextItem(nullptr)
{
	setFont(QETApp::indiTextsItemFont());
	QSettings settings;
	setRotation(settings.value("diagrameditor/independent_text_rotation", 0).toInt());
}

/**
	@brief IndependentTextItem::IndependentTextItem
	Constructeur
	@param text Le texte affiche par le champ de texte
*/
IndependentTextItem::IndependentTextItem(const QString &text) :
	DiagramTextItem(text, nullptr)
{}

/// Destructeur
IndependentTextItem::~IndependentTextItem()
{
}

/**
	Permet de lire le texte a mettre dans le champ a partir d'un element XML.
	Cette methode se base sur la position du champ pour assigner ou non la
	valeur a ce champ.
	@param e L'element XML representant le champ de texte
*/
void IndependentTextItem::fromXml(const QDomElement &e) {
	setPos(e.attribute("x").toDouble(), e.attribute("y").toDouble());
	if (e.hasAttribute("uuid"))
		m_uuid = QUuid(e.attribute("uuid"));
	/* les anciens fichiers stockent du balisage html complet, les textes
	 * bruts recents seulement la chaine : ne marquer "html" que ce qui
	 * l'est vraiment */
	const QString text = e.attribute("text");
	if (Qt::mightBeRichText(text)) {
		setHtml(text);
	} else {
		setPlainText(text);
	}
	setRotation(e.attribute("rotation").toDouble());
	if (e.hasAttribute("font"))
	{
		QFont font;
		font.fromString(e.attribute("font"));
		setFont(font);
	}
	if (e.hasAttribute("badge_bg"))
		setBadgeBackground(QColor(e.attribute("badge_bg")));
	if (e.hasAttribute("badge_border"))
		setBadgeBorder(QColor(e.attribute("badge_border")));
}

/**
	@param document Le document XML a utiliser
	@return L'element XML representant ce champ de texte
*/
QDomElement IndependentTextItem::toXml(QDomDocument &document) const
{
	QDomElement result = document.createElement("input");
	result.setAttribute("uuid", m_uuid.toString());
	result.setAttribute("x", QString("%1").arg(pos().x()));
	result.setAttribute("y", QString("%1").arg(pos().y()));
	// un texte brut est sauvegarde brut, pour rester brut au rechargement
	result.setAttribute("text", isHtml() ? toHtml() : toPlainText());
	result.setAttribute("rotation", QString::number(QET::correctAngle(rotation())));
	result.setAttribute("font", font().toString());
	if (badgeBackground().isValid())
		result.setAttribute("badge_bg",
			badgeBackground().name(QColor::HexArgb));
	if (badgeBorder().isValid())
		result.setAttribute("badge_border",
			badgeBorder().name(QColor::HexArgb));

	return(result);
}

void IndependentTextItem::focusOutEvent(QFocusEvent *event)
{
	DiagramTextItem::focusOutEvent(event);
	/* comparer dans la meme representation que celle memorisee au focusIn :
	 * un texte brut reste compare (et annule) en brut, sans jamais passer
	 * par du balisage html */
	const QString current_text = isHtml() ? toHtml() : toPlainText();
	if (diagram() && (m_previous_html_text != current_text)) {
		diagram()->undoStack().push(new ChangeDiagramTextCommand(this, m_previous_html_text, current_text));
	}
}

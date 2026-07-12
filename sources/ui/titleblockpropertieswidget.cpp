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
#include "titleblockpropertieswidget.h"

#include <algorithm>

#include "../Pdm/pdmsettings.h"
#include "../diagram.h"
#include "../qetapp.h"
#include "../qeticons.h"
#include "../titleblock/templatescollection.h"
#include "../titleblocktemplate.h"
#include "ui_titleblockpropertieswidget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <utility>

/**
	@brief TitleBlockPropertiesWidget::TitleBlockPropertiesWidget
	default constructor
	@param titleblock properties to edit
	@param current_date if true, display the radio button "current date"
	@param project : QETProject
	@param parent parent widget
*/
TitleBlockPropertiesWidget::TitleBlockPropertiesWidget(
		const TitleBlockProperties &titleblock,
		bool current_date,
		QETProject *project,
		QWidget *parent) :
	QWidget(parent),
	ui(new Ui::TitleBlockPropertiesWidget)
{
	ui->setupUi(this);
	initDialog(current_date, project);
	setProperties(titleblock);
}

/**
	@brief TitleBlockPropertiesWidget::TitleBlockPropertiesWidget
	default constructor with template list
	@param tbt_collection template list
	@param titleblock properties to edit
	@param current_date if true, display the radio button "current date"
	@param project : QETProject
	@param parent parent widget
*/
TitleBlockPropertiesWidget::TitleBlockPropertiesWidget(
		TitleBlockTemplatesCollection *tbt_collection,
		const TitleBlockProperties &titleblock,
		bool current_date,
		QETProject *project,
		QWidget *parent) :
	QWidget(parent),
	ui(new Ui::TitleBlockPropertiesWidget)
{
	ui->setupUi(this);
	initDialog(current_date,project);
	addCollection(tbt_collection);
	updateTemplateList();
	setProperties(titleblock);
}

/**
	@brief TitleBlockPropertiesWidget::TitleBlockPropertiesWidget
	Default constructor with several template collection
	@param tbt_collection template list
	@param titleblock properties to edit
	@param current_date if true, display the radio button "current date"
	@param project : QETProject
	@param parent parent widget
*/
TitleBlockPropertiesWidget::TitleBlockPropertiesWidget(
		QList<TitleBlockTemplatesCollection *> tbt_collection,
		const TitleBlockProperties &titleblock,
		bool current_date,
		QETProject *project,
		QWidget *parent) :
	QWidget(parent),
	ui(new Ui::TitleBlockPropertiesWidget)
{
	ui->setupUi(this);
	initDialog(current_date,project);
	foreach (TitleBlockTemplatesCollection *c, tbt_collection)
		addCollection(c);
	updateTemplateList();
	setProperties(titleblock);
}

/**
	@brief TitleBlockPropertiesWidget::~TitleBlockPropertiesWidget
	destructor
*/
TitleBlockPropertiesWidget::~TitleBlockPropertiesWidget()
{
	delete ui;
}

/**
	@brief TitleBlockPropertiesWidget::setProperties
	@param properties
*/
void TitleBlockPropertiesWidget::setProperties(
		const TitleBlockProperties &properties) {
	ui -> m_title_le      -> setText (properties.title);
	// 登入 PDM:作者固定為使用者帳號(建構子已設),不用檔案裡的舊作者覆蓋
	if (PdmSettings::username().isEmpty())
		ui -> m_author_le     -> setText (properties.author);
	ui -> m_file_le       -> setText (properties.filename);
	ui -> m_plant         -> setText (properties.plant);
	ui -> m_loc           -> setText (properties.locmach);
	ui -> m_indice        -> setText (properties.indexrev);
	ui -> m_folio_le      -> setText (properties.folio);
	ui -> m_display_at_cb -> setCurrentIndex(properties.display_at == Qt::BottomEdge ? 0 : 1);
	ui->auto_page_cb->setCurrentText(properties.auto_page_num);

	//About date
	ui -> m_date_now_pb -> setDisabled(true);
	ui -> m_date_edit   -> setDisabled(true);
	ui -> m_date_edit   -> setDate(QDate::currentDate());

	if (!ui -> m_current_date_rb -> isHidden()) {
		if(properties.useDate == TitleBlockProperties::CurrentDate)
			ui -> m_current_date_rb -> setChecked(true);
		else {
			if (properties.date.isNull())
				ui -> m_no_date_rb -> setChecked(true);
			else {
				ui -> m_fixed_date_rb -> setChecked(true);
				ui -> m_date_edit -> setDate(properties.date);
			}
		}
	}
	else {
		if (properties.useDate == TitleBlockProperties::CurrentDate)
			ui -> m_fixed_date_rb ->setChecked(true);
		else {
			if (properties.date.isNull())
				ui -> m_no_date_rb -> setChecked(true);
			else {
				ui -> m_fixed_date_rb -> setChecked(true);
				ui -> m_date_edit -> setDate(properties.date);
			}
		}
	} //About date

		//Set the current titleblock if any
	int index = 0;
	if (!properties.template_name.isEmpty())
	{
		index = getIndexFor(properties.template_name, properties.collection);
		if (index == -1) index = 0;
	}
	ui -> m_tbt_cb -> setCurrentIndex(index);

	/* champs societe dedies */
	for (auto it = m_company_fields.constBegin() ;
	     it != m_company_fields.constEnd() ; ++it) {
		it.value()->setText(
			properties.context.value(it.key()).toString());
	}
	if (m_doc_type_cb) {
		m_doc_type_cb->setEditText(properties.context
			.value(QStringLiteral("doc-type")).toString());
	}
	if (m_project_doc_id_le) {
		m_orig_doc_id = properties.context
				.value(QStringLiteral("doc-id")).toString();
		m_project_doc_id_le->setText(m_orig_doc_id);
	}
	for (auto it = m_project_fields.constBegin();
	     it != m_project_fields.constEnd(); ++it) {
		const QString v = properties.context.value(it.key()).toString();
		m_orig_project_fields.insert(it.key(), v);
		it.value()->setText(v);
	}

	/* les cles de revision sont editees via le dialogue dedie et les
	 * champs societe via les champs ci-dessus ; l'onglet
	 * « personnalise » ne montre que le reste */
	static const QRegularExpression rev_key(
		QStringLiteral("^rev[1-6]-(idx|date|zone|desc|by|appd)$"));
	m_reserved_context = DiagramContext();
	DiagramContext remainder;
	const QList<QString> keys = properties.context.keys();
	for (const QString &key : keys) {
		const bool show = properties.context.keyMustShow(key);
		if (rev_key.match(key).hasMatch()
		    || key == QLatin1String("doc-status")) {
			m_reserved_context.addValue(
				key, properties.context.value(key), show);
		} else if (!m_company_fields.contains(key)
			   && !m_project_fields.contains(key)
			   && key != QLatin1String("doc-type")
			   && !(m_project_doc_id_le
				&& key == QLatin1String("doc-id"))) {
			remainder.addValue(
				key, properties.context.value(key), show);
		}
	}
	m_dcw -> setContext(remainder);
}

/**
	@brief TitleBlockPropertiesWidget::properties
	@return the edited properties
*/
TitleBlockProperties TitleBlockPropertiesWidget::properties() const
{
	TitleBlockProperties prop;
	prop.title      = ui -> m_title_le      -> text();
	prop.author     = ui -> m_author_le     -> text();
	prop.filename   = ui -> m_file_le       -> text();
	prop.plant      = ui -> m_plant         -> text();
	prop.locmach    = ui -> m_loc           -> text();
	prop.indexrev   = ui -> m_indice        -> text();
	prop.folio      = ui -> m_folio_le      -> text();
	prop.display_at = ui -> m_display_at_cb -> currentIndex() == 0 ? Qt::BottomEdge : Qt::RightEdge;

	if (ui->m_no_date_rb->isChecked()) {
		prop.useDate = TitleBlockProperties::UseDateValue;
		prop.date = QDate();
	}
	else if (ui -> m_fixed_date_rb -> isChecked()) {
		prop.useDate = TitleBlockProperties::UseDateValue;
		prop.date = ui->m_date_edit->date();
	}
	else if (ui->m_current_date_rb->isVisible() && ui->m_current_date_rb->isChecked()) {
		prop.useDate = TitleBlockProperties::CurrentDate;
		prop.date = QDate::currentDate();
	}

	if (!currentTitleBlockTemplateName().isEmpty())
	{
		prop.template_name = currentTitleBlockTemplateName();
		prop.collection = m_map_index_to_collection_type.at(ui->m_tbt_cb->currentIndex());
	}

	prop.context = m_dcw -> context();
	applyCompanyFields(prop);

	/* champs « projet » : appliques immediatement a tous les folios */
	if (m_project && m_project_title_le) {
		if (m_project_title_le->text() != m_orig_project_title) {
			m_project->setTitle(m_project_title_le->text());
		}
		const QString doc_id = m_project_doc_id_le->text();
		if (doc_id != m_orig_doc_id) {
			const auto diagram_list = m_project->diagrams();
			for (Diagram *d : diagram_list) {
				TitleBlockProperties p =
					d->border_and_titleblock.exportTitleBlock();
				p.context.addValue(QStringLiteral("doc-id"), doc_id);
				d->border_and_titleblock.importTitleBlock(p);
				d->update();
			}
		}
		// 專案共用欄位:有變更即套用到全專案所有頁(同 doc-id)
		for (auto it = m_project_fields.constBegin();
		     it != m_project_fields.constEnd(); ++it) {
			if (it.value()->text()
			    == m_orig_project_fields.value(it.key()))
				continue;
			const auto diagram_list = m_project->diagrams();
			for (Diagram *d : diagram_list) {
				TitleBlockProperties p =
					d->border_and_titleblock.exportTitleBlock();
				p.context.addValue(it.key(), it.value()->text());
				d->border_and_titleblock.importTitleBlock(p);
				d->update();
			}
		}
	}

	prop.auto_page_num = ui->auto_page_cb->currentText();

	return prop;
}

/**
	@brief TitleBlockPropertiesWidget::properties
	@return return properties to enable folio autonum
*/
TitleBlockProperties TitleBlockPropertiesWidget::propertiesAutoNum(
		QString autoNum) const
{
	TitleBlockProperties prop;
	prop.title    = ui -> m_title_le  -> text();
	prop.author   = ui -> m_author_le -> text();
	prop.filename = ui -> m_file_le   -> text();
	prop.plant    = ui -> m_plant     -> text();
	prop.locmach  = ui -> m_loc       -> text();
	prop.indexrev = ui -> m_indice    -> text();
	prop.folio    = "%autonum";
	prop.display_at = ui -> m_display_at_cb -> currentIndex() == 0 ? Qt::BottomEdge : Qt::RightEdge;

	if (ui->m_no_date_rb->isChecked()) {
		prop.useDate = TitleBlockProperties::UseDateValue;
		prop.date = QDate();
	}
	else if (ui -> m_fixed_date_rb -> isChecked()) {
		prop.useDate = TitleBlockProperties::UseDateValue;
		prop.date = ui->m_date_edit->date();
	}
	else if (ui->m_current_date_rb->isVisible() && ui->m_current_date_rb->isChecked()) {
		prop.useDate = TitleBlockProperties::CurrentDate;
		prop.date = QDate::currentDate();
	}

	if (!currentTitleBlockTemplateName().isEmpty())
	{
		prop.template_name = currentTitleBlockTemplateName();
		prop.collection = m_map_index_to_collection_type.at(ui->m_tbt_cb->currentIndex());
	}

	prop.context = m_dcw -> context();
	applyCompanyFields(prop);

	prop.auto_page_num = std::move(autoNum);

	return prop;
}

TitleBlockTemplateLocation TitleBlockPropertiesWidget::currentTitleBlockLocation() const
{
	QET::QetCollection qc = m_map_index_to_collection_type.at(ui->m_tbt_cb->currentIndex());
	TitleBlockTemplatesCollection *collection = nullptr;
	foreach (TitleBlockTemplatesCollection *c, m_tbt_collection_list)
		if (c -> collection() == qc)
			collection = c;

	if (!collection)
		return TitleBlockTemplateLocation();

	return collection->location(currentTitleBlockTemplateName());
}

/**
	@brief TitleBlockPropertiesWidget::setTitleBlockTemplatesVisible
	if true, title block template combo box and menu button is visible
*/
void TitleBlockPropertiesWidget::setTitleBlockTemplatesVisible(
		const bool &visible)
{
	ui -> m_tbt_label -> setVisible(visible);
	ui -> m_tbt_cb    -> setVisible(visible);
	ui -> m_tbt_pb    -> setVisible(visible);
}

/**
	@brief TitleBlockPropertiesWidget::setReadOnly
	if true, this widget is disable
*/
void TitleBlockPropertiesWidget::setReadOnly(const bool &ro) {
	ui->m_tbt_gb->setDisabled(ro);
}

/**
	@brief TitleBlockPropertiesWidget::currentTitleBlockTemplateName
	@return the current title block name
*/
QString TitleBlockPropertiesWidget::currentTitleBlockTemplateName() const
{
	int index = ui -> m_tbt_cb -> currentIndex();
	if(index != -1)
		return (ui -> m_tbt_cb -> itemData(index).toString());
	return QString();
}

/**
	@brief TitleBlockPropertiesWidget::addCollection
	add a collection of title block available in the combo box
	@param tbt_collection
*/
void TitleBlockPropertiesWidget::addCollection(
		TitleBlockTemplatesCollection *tbt_collection)
{
	if (!tbt_collection || m_tbt_collection_list.contains(tbt_collection))
		return;
	m_tbt_collection_list << tbt_collection;
}

/**
	@brief TitleBlockPropertiesWidget::initDialog
	Init this dialog
	@param current_date : true for display current date radio button
	@param project
*/
void TitleBlockPropertiesWidget::initDialog(
		const bool &current_date,QETProject *project)
{
	m_dcw = new DiagramContextWidget();
	ui -> m_tab2_vlayout -> addWidget(m_dcw);

	/* adaptation cartouche societe : masquer les champs standards non
	 * utilises et exposer les champs du cartouche societe en premiere
	 * classe (stockes dans le contexte personnalise) */
	ui -> label_5  -> hide();  ui -> m_file_le  -> hide();
	ui -> label_6  -> hide();  ui -> m_folio_le -> hide();
	ui -> label_10 -> hide();  ui -> m_plant    -> hide();
	ui -> label_11 -> hide();  ui -> m_loc      -> hide();
	/* date de publication et indice de revision : geres par le
	 * dialogue « revisions du folio » (valeurs preservees) */
	ui -> label_4  -> hide();
	ui -> m_no_date_rb -> hide();
	ui -> m_fixed_date_rb -> hide();
	ui -> m_current_date_rb -> hide();
	ui -> m_date_edit -> hide();
	ui -> m_date_now_pb -> hide();
	ui -> label_12 -> hide();  ui -> m_indice -> hide();

	auto *company_form = new QFormLayout();
	/* type de document : codes DCC (IEC 61355, §5.2 du guide societe) */
	m_doc_type_cb = new QComboBox(this);
	m_doc_type_cb->setEditable(true);
	m_doc_type_cb->addItems({
		QStringLiteral("&EFS 電路圖"),
		QStringLiteral("&EPB 零件清單"),
		QStringLiteral("&ELD 佈置圖"),
		QStringLiteral("&EMB 接線表"),
		QStringLiteral("&EMA 接線圖"),
		QStringLiteral("&EFA 概觀圖/單線圖"),
	});
	company_form->addRow(tr("Type de document :"), m_doc_type_cb);
	const QList<QPair<QString, QString>> company_keys {
		{QStringLiteral("techref"),     tr("Référence technique :")},
		{QStringLiteral("checked-by"),  tr("Vérifié par :")},
		{QStringLiteral("approved-by"), tr("Approuvé par :")},
		{QStringLiteral("remarks"),     tr("Remarques :")},
	};
	const bool pdm_logged_in = !PdmSettings::username().isEmpty();
	// 唯讀欄位改用視窗底色(非白),讓使用者一眼看出不可編輯。
	const QString readonly_style =
		QStringLiteral("QLineEdit{background-color:palette(window);"
			       "color:palette(dark);}");
	for (const auto &pair : company_keys) {
		auto *edit = new QLineEdit(this);
		m_company_fields.insert(pair.first, edit);
		const bool signing = pair.first == QLatin1String("checked-by")
			|| pair.first == QLatin1String("approved-by");
		// 登入 PDM:審核者/核准者/備註由系統於審核發行流程管理,整列隱藏
		//(欄位物件保留,既有值原樣 round-trip 保存);未登入才顯示唯讀簽核欄。
		if (pdm_logged_in
		    && (signing || pair.first == QLatin1String("remarks"))) {
			edit->hide();
			continue;
		}
		if (signing) {
			edit->setReadOnly(true);
			edit->setStyleSheet(readonly_style);
			edit->setToolTip(
				tr("由圖檔管理自動設定,不可手動修改"));
		}
		company_form->addRow(pair.second, edit);
	}
	// 登入 PDM:作者自動帶入使用者帳號且不可修改
	if (pdm_logged_in) {
		ui->m_author_le->setText(PdmSettings::username());
		ui->m_author_le->setReadOnly(true);
		ui->m_author_le->setStyleSheet(readonly_style);
	}
	ui -> verticalLayout_2 -> addLayout(company_form);

	/* dans le cartouche societe, la cellule « titre complementaire »
	 * affiche %{title} (le titre du folio) : renommer le champ */
	ui -> label_2 -> setText(tr("Sous-titre :"));

	/* onglets : « folio » pour les champs propres a la page,
	 * « projet » pour les champs communs a tous les folios */
	ui -> tabWidget -> setTabText(0, tr("Folio", "tab title"));
	m_project = project;
	if (project) {
		auto *project_page = new QWidget(this);
		auto *project_form = new QFormLayout(project_page);
		m_project_title_le = new QLineEdit(project->title(), project_page);
		project_form->addRow(tr("Titre du projet :"), m_project_title_le);
		m_project_doc_id_le = new QLineEdit(project_page);
		project_form->addRow(tr("Numéro de document :"), m_project_doc_id_le);
		// 專案共用欄位(對應封面圖框變數):改一次套用到全專案所有頁
		const struct { QString key; QString label; } proj_fields[] = {
			{QStringLiteral("customer"), tr("客戶名稱 :")},
			{QStringLiteral("pm"),       tr("專案管理 :")},
			{QStringLiteral("mech"),     tr("機構擔當 :")},
			{QStringLiteral("elec"),     tr("電控擔當 :")},
			{QStringLiteral("sw"),       tr("軟體擔當 :")},
		};
		for (const auto &f : proj_fields) {
			auto *le = new QLineEdit(project_page);
			project_form->addRow(f.label, le);
			m_project_fields.insert(f.key, le);
		}
		auto *note = new QLabel(
			tr("Ces champs sont communs à tous les folios du projet."),
			project_page);
		note->setWordWrap(true);
		project_form->addRow(note);
		ui -> tabWidget -> insertTab(1, project_page, tr("Projet", "tab title"));
		m_orig_project_title = project->title();
	}

	setTitleBlockTemplatesVisible(false);
	ui -> m_current_date_rb -> setVisible(current_date);

	m_tbt_edit = new QAction(tr("Éditer ce modèle", "menu entry"), this);
	m_tbt_duplicate = new QAction(tr("Dupliquer et éditer ce modèle",
					 "menu entry"),
				      this);

	connect(m_tbt_edit,
		SIGNAL(triggered()),
		this,
		SLOT(editCurrentTitleBlockTemplate()));
	connect(m_tbt_duplicate,
		SIGNAL(triggered()),
		this,
		SLOT(duplicateCurrentTitleBlockTemplate()));

	m_tbt_menu = new QMenu(tr("Title block templates actions"), ui->m_tbt_pb);
	m_tbt_menu -> addAction(m_tbt_edit);
	m_tbt_menu -> addAction(m_tbt_duplicate);
	ui -> m_tbt_pb -> setMenu(m_tbt_menu);

	connect(ui->m_tbt_cb,
		SIGNAL(currentIndexChanged(int)),
		this,
		SLOT(changeCurrentTitleBlockTemplate(int)));

	if (project!= nullptr){
		keys_2 = project -> folioAutoNum().keys();
		std::sort(keys_2.begin(), keys_2.end());   // 規則清單依名稱排序
		foreach (QString str, keys_2) { ui -> auto_page_cb -> addItem(str); }
		if (ui->auto_page_cb->currentText()==nullptr)
			ui->auto_page_cb->addItem(tr("Créer un Folio Numérotation Auto"));
	}
	else{
		ui->auto_page_cb->hide();
		ui->m_edit_autofolionum_pb->hide();
		ui->label_9->hide();
	}

}

/**
	@brief TitleBlockPropertiesWidget::getIndexFor
	Find the index of the combo box for
	the title block tbt_name available on the collection collection
	@param tbt_name : title block name
	@param collection : title block collection
	@return the index of the title block or -1 if no match
*/
int TitleBlockPropertiesWidget::getIndexFor(
		const QString &tbt_name,
		const QET::QetCollection collection) const
{
	for (int i = 0; i<ui->m_tbt_cb->count(); i++) {
		if (ui->m_tbt_cb->itemData(i).toString() == tbt_name)
			if (m_map_index_to_collection_type.at(i) == collection)
				return i;
	}
	return -1;
}

void TitleBlockPropertiesWidget::editCurrentTitleBlockTemplate()
{
	QETApp::instance()->openTitleBlockTemplate(currentTitleBlockLocation(), false);
}

void TitleBlockPropertiesWidget::duplicateCurrentTitleBlockTemplate()
{
	QETApp::instance()->openTitleBlockTemplate(currentTitleBlockLocation(), true);
}

/**
	@brief TitleBlockPropertiesWidget::updateTemplateList
	Update the title block template list available in the combo box
*/
void TitleBlockPropertiesWidget::updateTemplateList()
{
	ui -> m_tbt_cb ->clear();

	if (m_tbt_collection_list.isEmpty())
	{
		setTitleBlockTemplatesVisible(false);
		return;
	}
	setTitleBlockTemplatesVisible(true);

		//Add the default title block
	m_map_index_to_collection_type.clear();
	m_map_index_to_collection_type.append(QET::QetCollection::Common);
	ui -> m_tbt_cb -> addItem(QET::Icons::QETLogo, tr("Modèle par défaut"));

		//Add every title block stored in m_tbt_collection_list
	foreach (TitleBlockTemplatesCollection *tbt_c, m_tbt_collection_list)
	{
		QIcon icon;
		QET::QetCollection qc = tbt_c -> collection();
		if (qc == QET::QetCollection::Common)
			icon = QET::Icons::QETLogo;
		else if (qc == QET::QetCollection::Company)
			icon = QET::Icons::Company;
		else if (qc == QET::QetCollection::Custom)
			icon = QET::Icons::Home;
		else if (qc == QET::QetCollection::Embedded)
			icon = QET::Icons::TitleBlock;

		foreach(QString tbt_name, tbt_c -> templates())
		{
			m_map_index_to_collection_type.append(qc);
			ui -> m_tbt_cb -> addItem(icon, tbt_name, tbt_name);
		}
	}
}

/**
	@brief TitleBlockPropertiesWidget::changeCurrentTitleBlockTemplate
	Load the additional field of title block "text"
*/
void TitleBlockPropertiesWidget::changeCurrentTitleBlockTemplate(int index)
{
	m_dcw -> clear();

	QET::QetCollection qc = m_map_index_to_collection_type.at(index);
	TitleBlockTemplatesCollection *collection = nullptr;
	foreach (TitleBlockTemplatesCollection *c, m_tbt_collection_list)
		if (c -> collection() == qc)
			collection = c;

	if (!collection) return;

		// get template
	TitleBlockTemplate *tpl = collection -> getTemplate(ui -> m_tbt_cb -> currentText());
	if(tpl != nullptr) {
			// get all template fields
		QStringList fields = tpl -> listOfVariables();
			// set fields to additional_fields_ widget
		DiagramContext templateContext;
		for(int i =0; i<fields.count(); i++)
			templateContext.addValue(fields.at(i), "");
		m_dcw -> setContext(templateContext);
	}
}

/**
	@brief TitleBlockPropertiesWidget::on_m_date_now_pb_clicked
	Set the date to current date
*/
void TitleBlockPropertiesWidget::on_m_date_now_pb_clicked()
{
	ui -> m_date_edit -> setDate(QDate::currentDate());
}

/**
	@brief TitleBlockPropertiesWidget::on_m_edit_autofolionum_pb_clicked
	Open Auto Folio Num dialog
*/
void TitleBlockPropertiesWidget::on_m_edit_autofolionum_pb_clicked()
{
	emit openAutoNumFolioEditor(ui->auto_page_cb->currentText());
	if (ui->auto_page_cb->currentText()!=tr("Créer un Folio Numérotation Auto"))
	{
		//still to implement: load current auto folio num settings
	}
}

/**
	Reinjecte dans \a properties les cles gerees hors de l'onglet
	« personnalise » : les lignes de revision (preservees telles quelles,
	editees via le dialogue dedie) et les champs societe dedies.
*/
void TitleBlockPropertiesWidget::applyCompanyFields(TitleBlockProperties &properties) const
{
	const QList<QString> reserved_keys = m_reserved_context.keys();
	for (const QString &key : reserved_keys) {
		properties.context.addValue(
			key,
			m_reserved_context.value(key),
			m_reserved_context.keyMustShow(key));
	}
	for (auto it = m_company_fields.constBegin() ;
	     it != m_company_fields.constEnd() ; ++it) {
		properties.context.addValue(it.key(), it.value()->text());
	}
	if (m_doc_type_cb) {
		properties.context.addValue(QStringLiteral("doc-type"),
					    m_doc_type_cb->currentText());
	}
	if (m_project_doc_id_le) {
		properties.context.addValue(QStringLiteral("doc-id"),
					    m_project_doc_id_le->text());
	}
	for (auto it = m_project_fields.constBegin();
	     it != m_project_fields.constEnd(); ++it) {
		properties.context.addValue(it.key(), it.value()->text());
	}
}

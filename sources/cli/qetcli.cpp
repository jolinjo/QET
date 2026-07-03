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
#include "qetcli.h"

#include "../diagram.h"
#include "../qetgraphicsitem/conductor.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"
#include "../qetproject.h"

#include <QApplication>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <cstdio>
#include <cstring>

namespace {

struct CliOptions {
	QString command;   // validate / netlist / render
	QString file;
	QString out;
	int folio = -1;    // -1 = all folios
	int width = -1;    // -1 = natural size
	QString error;
};

void printJson(const QJsonObject &obj)
{
	std::puts(QJsonDocument(obj)
		.toJson(QJsonDocument::Indented).constData());
}

int fail(const QString &message)
{
	printJson({{"ok", false}, {"error", message}});
	return 1;
}

CliOptions parseArguments(const QStringList &args)
{
	CliOptions opt;
	for (int i = 1; i < args.size(); ++i) {
		const QString &a = args.at(i);
		auto value = [&]() -> QString {
			return (i + 1 < args.size()) ? args.at(++i) : QString();
		};
		if      (a == QLatin1String("--cli-validate")) opt.command = "validate";
		else if (a == QLatin1String("--cli-netlist"))  opt.command = "netlist";
		else if (a == QLatin1String("--cli-render"))   opt.command = "render";
		else if (a == QLatin1String("--cli-export-pdf")) opt.command = "exportpdf";
		else if (a == QLatin1String("--out"))          opt.out = value();
		else if (a == QLatin1String("--folio"))        opt.folio = value().toInt();
		else if (a == QLatin1String("--width"))        opt.width = value().toInt();
		else if (!a.startsWith(QLatin1String("-")))    opt.file = a;
		else opt.error = QStringLiteral("unknown option: %1").arg(a);
	}
	if (opt.command.isEmpty())
		opt.error = QStringLiteral("no --cli-* command given");
	else if (opt.file.isEmpty())
		opt.error = QStringLiteral("no project file given");
	else if (opt.command == QLatin1String("render") && opt.out.isEmpty())
		opt.error = QStringLiteral("--cli-render requires --out <base.png>");
	else if (opt.command == QLatin1String("exportpdf") && opt.out.isEmpty())
		opt.error = QStringLiteral("--cli-export-pdf requires --out <file.pdf>");
	return opt;
}

QString uuidString(const QUuid &uuid)
{
	return uuid.toString(QUuid::WithBraces);
}

QJsonObject folioSummary(Diagram *diagram)
{
	return {
		{"title", diagram->title()},
		{"elements", diagram->elements().count()},
		{"conductors", diagram->conductors().count()},
	};
}

int cmdValidate(QETProject *project)
{
	QJsonArray folios;
	const auto diagrams = project->diagrams();
	for (Diagram *d : diagrams)
		folios.append(folioSummary(d));
	printJson({
		{"ok", true},
		{"title", project->title()},
		{"folios", folios},
	});
	return 0;
}

QJsonObject terminalEndpoint(Terminal *terminal)
{
	QJsonObject endpoint{{"terminal", uuidString(terminal->uuid())}};
	if (auto *element = dynamic_cast<Element *>(terminal->parentItem())) {
		endpoint.insert("element", uuidString(element->uuid()));
		endpoint.insert("label", element->actualLabel());
	}
	return endpoint;
}

int cmdNetlist(QETProject *project)
{
	QJsonArray folios;
	const auto diagrams = project->diagrams();
	for (Diagram *diagram : diagrams) {
		QJsonArray elements;
		const auto elmts = diagram->elements();
		for (Element *element : elmts) {
			elements.append(QJsonObject{
				{"uuid", uuidString(element->uuid())},
				{"label", element->actualLabel()},
			});
		}
		QJsonArray nets;
		const auto conductors = diagram->conductors();
		for (Conductor *conductor : conductors) {
			nets.append(QJsonObject{
				{"num", conductor->properties().text},
				{"a", terminalEndpoint(conductor->terminal1)},
				{"b", terminalEndpoint(conductor->terminal2)},
			});
		}
		folios.append(QJsonObject{
			{"title", diagram->title()},
			{"elements", elements},
			{"nets", nets},
		});
	}
	printJson({{"ok", true}, {"folios", folios}});
	return 0;
}

int cmdRender(QETProject *project, const CliOptions &opt)
{
	const auto diagrams = project->diagrams();
	QJsonArray rendered;
	for (int i = 0; i < diagrams.count(); ++i) {
		if (opt.folio >= 0 && opt.folio != i) continue;
		Diagram *diagram = diagrams.at(i);

		QSize size = diagram->imageSize();
		if (opt.width > 0)
			size = QSize(opt.width,
			             size.height() * opt.width / qMax(1, size.width()));

		QString path = opt.out;
		if (opt.folio < 0 && diagrams.count() > 1) {
			const int dot = path.lastIndexOf(QLatin1Char('.'));
			path.insert(dot < 0 ? path.size() : dot,
			            QStringLiteral("_%1").arg(i + 1));
		}

		if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
			// PDF at the folio's true size (folio scale is 4 px/mm),
			// zero margins → prints with no extra whitespace, correct
			// aspect ratio, and vector output.
			const QSize folio = diagram->imageSize();
			const double px_to_pt = 72.0 / (25.4 * 4.0);
			QPdfWriter pdf(path);
			pdf.setPageSize(QPageSize(
				QSizeF(folio.width() * px_to_pt,
				       folio.height() * px_to_pt),
				QPageSize::Point, QStringLiteral("folio"),
				QPageSize::ExactMatch));
			pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
			if (!diagram->toPaintDevice(pdf, pdf.width(), pdf.height()))
				return fail(QStringLiteral("rendering folio %1 to PDF "
				            "failed").arg(i));
			rendered.append(QJsonObject{
				{"folio", i}, {"file", path}, {"format", "pdf"},
				{"width", folio.width()}, {"height", folio.height()},
			});
			continue;
		}

		QImage image(size, QImage::Format_RGB32);
		image.fill(Qt::white);
		if (!diagram->toPaintDevice(image, size.width(), size.height()))
			return fail(QStringLiteral("rendering folio %1 failed").arg(i));
		if (!image.save(path))
			return fail(QStringLiteral("cannot write %1").arg(path));
		rendered.append(QJsonObject{
			{"folio", i},
			{"file", path},
			{"width", size.width()},
			{"height", size.height()},
		});
	}
	printJson({{"ok", true}, {"rendered", rendered}});
	return 0;
}

/*
	Export via the application's real print pipeline (QPrinter in PDF
	mode + fit-to-page scene render) so print-only rendering issues can
	be reproduced headlessly.
*/
int cmdExportPdf(QETProject *project, const CliOptions &opt)
{
	const auto diagrams = project->diagrams();
	QPrinter printer;
	printer.setOutputFormat(QPrinter::PdfFormat);
	printer.setOutputFileName(opt.out);
	printer.setPageSize(QPageSize(QPageSize::A4));
	printer.setPageOrientation(QPageLayout::Landscape);

	QPainter painter;
	if (!painter.begin(&printer))
		return fail(QStringLiteral("cannot open printer painter"));

	bool first = true;
	for (int i = 0; i < diagrams.count(); ++i) {
		if (opt.folio >= 0 && opt.folio != i) continue;
		Diagram *diagram = diagrams.at(i);
		if (!first) printer.newPage();
		first = false;
		QRectF diagram_rect = diagram
			->border_and_titleblock.borderAndTitleBlockRect();
		diagram->render(&painter, QRectF(), diagram_rect,
				Qt::KeepAspectRatio);
	}
	painter.end();
	printJson({{"ok", true}, {"file", opt.out}});
	return 0;
}

} // anonymous namespace

namespace QetCli {

bool isCliInvocation(int argc, char **argv)
{
	for (int i = 1; i < argc; ++i)
		if (std::strncmp(argv[i], "--cli-", 6) == 0) return true;
	return false;
}

int run(int argc, char **argv)
{
	// Headless by default; an explicitly chosen platform is respected.
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);

	const CliOptions opt = parseArguments(app.arguments());
	if (!opt.error.isEmpty()) return fail(opt.error);

	QETProject project(opt.file);
	if (project.state() != QETProject::Ok)
		return fail(QStringLiteral("cannot open %1 (state %2)")
		            .arg(opt.file).arg(project.state()));

	if (opt.command == QLatin1String("validate")) return cmdValidate(&project);
	if (opt.command == QLatin1String("netlist"))  return cmdNetlist(&project);
	if (opt.command == QLatin1String("exportpdf")) return cmdExportPdf(&project, opt);
	return cmdRender(&project, opt);
}

} // namespace QetCli

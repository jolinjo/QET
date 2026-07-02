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
#ifndef QETCLI_H
#define QETCLI_H

/**
	Headless command-line mode (no GUI, no single-instance logic).

	Usage:
	  qelectrotech --cli-validate <file.qet>
	  qelectrotech --cli-netlist  <file.qet>
	  qelectrotech --cli-render   <file.qet> --out <base.png>
	                              [--folio N] [--width W]

	All results are printed to stdout as JSON (render also writes PNG
	files). Exit code 0 on success.
*/
namespace QetCli {
	/// @return true if argv requests CLI mode (any "--cli-*" argument)
	bool isCliInvocation(int argc, char **argv);

	/// Run the CLI. Creates its own offscreen QApplication.
	int run(int argc, char **argv);
}

#endif // QETCLI_H

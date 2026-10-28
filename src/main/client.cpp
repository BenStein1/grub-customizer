/*
 * Copyright (C) 2010-2011 Daniel Richter <danielrichter2007@web.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 * 
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this program, or any covered work, by linking or combining
 * it with the OpenSSL library (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL license, the licensors
 * of this program grant you additional permission to convey the resulting work.
 * Corresponding source for a non-source form of such a combination shall include
 * the source code for the parts of the OpenSSL library used as well as that of
 * the covered work.
 */

#include <iostream>
#include <vector>
#include <cstdlib>
#include <climits>
#include <sys/wait.h>
#include <unistd.h>

#include "../Bootstrap/View.hpp"
#include "../Bootstrap/Application.hpp"
#include "../Bootstrap/Factory.hpp"
#include "../lib/ContentParser/Chainloader.hpp"
#include "../lib/ContentParser/FactoryImpl.hpp"
#include "../lib/ContentParser/Linux.hpp"
#include "../lib/ContentParser/LinuxIso.hpp"
#include "../lib/ContentParser/Memtest.hpp"
#include "../Controller/Helper/RuleMover/Strategy/MoveRuleOnSameLevelInsideProxy.hpp"
#include "../Controller/Helper/RuleMover/Strategy/MoveRuleIntoSubmenu.hpp"
#include "../Controller/Helper/RuleMover/Strategy/MoveRuleOutOfSubmenu.hpp"
#include "../Controller/Helper/RuleMover/Strategy/MoveRuleOutOfProxyOnToplevel.hpp"
#include "../Controller/Helper/RuleMover/Strategy/MoveRuleIntoForeignSubmenu.hpp"
#include "../Controller/Helper/RuleMover/Strategy/MoveForeignRuleFromSubmenuToToplevel.hpp"
#include "../lib/Logger/Stream.hpp"
#include "../Mapper/EntryNameImpl.hpp"
#include "../config.hpp"
#include "../lib/Helper.hpp"
#include "../Controller/AboutController.hpp"
#include "../Controller/EntryEditController.hpp"
#include "../Controller/EnvEditorController.hpp"
#include "../Controller/ErrorController.hpp"
#include "../Controller/InstallerController.hpp"
#include "../Controller/MainController.hpp"
#include "../Controller/SettingsController.hpp"
#include "../Controller/ThemeController.hpp"
#include "../Controller/TrashController.hpp"



static std::string const WAYLAND_SOCKET_PARAM = "--wayland-socket=";

/**
 * restarts Grub Customizer as root using pkexec. pkexec only passes DISPLAY and XAUTHORITY
 * (see allow_gui in the polkit policy), so the root process normally runs on X11/Xwayland.
 * If there's no X server (wayland session without Xwayland) the wayland socket is passed instead.
 */
static int runAsRoot(int argc, char** argv)
{
	// pkexec matches the policy by the absolute path of the binary
	std::string binary = argv[0];
	char* exePath = realpath("/proc/self/exe", nullptr);
	if (exePath) {
		binary = exePath;
		free(exePath);
	}

	char const* display = getenv("DISPLAY");
	char const* waylandDisplay = getenv("WAYLAND_DISPLAY");
	char const* runtimeDir = getenv("XDG_RUNTIME_DIR");
	bool useX = display != nullptr && std::string(display) != "";

	std::string cmd = "pkexec " + Helper::shellQuote(binary);
	for (int i = 1; i < argc; i++) {
		cmd += " " + Helper::shellQuote(argv[i]);
	}
	if (!useX && waylandDisplay != nullptr && std::string(waylandDisplay) != "") {
		std::string socket = waylandDisplay;
		if (socket[0] != '/' && runtimeDir != nullptr) {
			socket = std::string(runtimeDir) + "/" + socket;
		}
		cmd += " " + Helper::shellQuote(WAYLAND_SOCKET_PARAM + socket);
	}
	cmd += " no-fork";

	if (useX) {
		// allow root to connect to the (X)Wayland server. xhost is optional - pkexec also passes XAUTHORITY
		cmd = "if command -v xhost >/dev/null 2>&1; then xhost +SI:localuser:root >/dev/null; fi; "
			+ cmd + "; status=$?; "
			"if command -v xhost >/dev/null 2>&1; then xhost -SI:localuser:root >/dev/null; fi; "
			"exit $status";
	}

	int status = system(cmd.c_str());
	if (status == -1 || !WIFEXITED(status)) {
		return 1;
	}
	return WEXITSTATUS(status);
}

int main(int argc, char** argv){
	if (argc == 2 && (std::string(argv[1]) == "-v" || std::string(argv[1]) == "--version")) {
		std::cout << "Grub Customizer, version " << GC_VERSION << std::endl;
		return 0;
	}

	if (getuid() != 0 && (argc == 1 || argv[argc - 1] != std::string("no-fork"))) {
		return runAsRoot(argc, argv);
	}

	// remove internal parameters
	std::vector<char*> params;
	for (int i = 0; i < argc; i++) {
		std::string param = argv[i];
		if (i != 0 && param.substr(0, WAYLAND_SOCKET_PARAM.length()) == WAYLAND_SOCKET_PARAM) {
			if (getenv("DISPLAY") == nullptr || std::string(getenv("DISPLAY")) == "") {
				setenv("WAYLAND_DISPLAY", param.substr(WAYLAND_SOCKET_PARAM.length()).c_str(), 1);
			}
			continue;
		}
		if (i != 0 && param == "no-fork") {
			continue;
		}
		params.push_back(argv[i]);
	}
	params.push_back(nullptr);
	argc = params.size() - 1;
	argv = params.data();

	if ((getenv("DISPLAY") == nullptr || std::string(getenv("DISPLAY")) == "") && (getenv("WAYLAND_DISPLAY") == nullptr || std::string(getenv("WAYLAND_DISPLAY")) == "")) {
		std::cerr << "Grub Customizer: no display found. Please start grub-customizer as a normal user "
			"(without sudo) - it will ask for the password using polkit." << std::endl;
		return 1;
	}
	setlocale(LC_ALL, "");
	bindtextdomain("grub-customizer", LOCALEDIR);
	textdomain("grub-customizer");

	auto logger = std::make_shared<Logger_Stream>(std::cout);

	Logger::getInstance() = logger;

	try {
		auto application          = std::make_shared<Bootstrap_Application>(argc, argv);
		auto view                 = std::make_shared<Bootstrap_View>();
		auto factory              = std::make_shared<Bootstrap_Factory>(application->applicationObject, logger);

		auto settingsOnDisk       = factory->create<Model_SettingsManagerData>();
		auto savedListCfg         = factory->create<Model_ListCfg>();

		factory->entryNameMapper->setView(view->main);

		auto entryEditController = factory->createController<EntryEditController>(view->entryEditor);
		auto mainController      = factory->createController<MainController>(view->main);
		auto settingsController  = factory->createController<SettingsController>(view->settings);
		auto envEditController   = factory->createController<EnvEditorController>(view->envEditor);
		auto trashController     = factory->createController<TrashController>(view->trash);
		auto installController   = factory->createController<InstallerController>(view->installer);
		auto aboutController     = factory->createController<AboutController>(view->about);
		auto errorController     = factory->createController<ErrorController>(view->error);
		auto themeController     = factory->createController<ThemeController>(view->theme);

		mainController->setSettingsBuffer(settingsOnDisk);
		mainController->setSavedListCfg(savedListCfg);

		// configure logger
		logger->setLogLevel(Logger_Stream::LOG_EVENT);
		if (argc > 1) {
			std::string logParam = argv[1];
			if (logParam == "debug") {
				logger->setLogLevel(Logger_Stream::LOG_DEBUG_ONLY);
			} else if (logParam == "log-important") {
				logger->setLogLevel(Logger_Stream::LOG_IMPORTANT);
			} else if (logParam == "quiet") {
				logger->setLogLevel(Logger_Stream::LOG_NOTHING);
			} else if (logParam == "verbose") {
				logger->setLogLevel(Logger_Stream::LOG_VERBOSE);
			}
		}

		factory->contentParserFactory->registerParser(factory->create<ContentParser_Linux>(), gettext("Linux"));
		factory->contentParserFactory->registerParser(factory->create<ContentParser_LinuxIso>(), gettext("Linux-ISO"));
		factory->contentParserFactory->registerParser(factory->create<ContentParser_Chainloader>(), gettext("Chainloader"));
		factory->contentParserFactory->registerParser(factory->create<ContentParser_Memtest>(), gettext("Memtest"));

		view->entryEditor->setAvailableEntryTypes(factory->contentParserFactory->getNames());

		factory->ruleMover->addStrategy(factory->create<Controller_Helper_RuleMover_Strategy_MoveRuleIntoSubmenu>());
		factory->ruleMover->addStrategy(factory->create<Controller_Helper_RuleMover_Strategy_MoveRuleOnSameLevelInsideProxy>());
		factory->ruleMover->addStrategy(factory->create<Controller_Helper_RuleMover_Strategy_MoveForeignRuleFromSubmenuToToplevel>());
		factory->ruleMover->addStrategy(factory->create<Controller_Helper_RuleMover_Strategy_MoveRuleOutOfSubmenu>());
		factory->ruleMover->addStrategy(factory->create<Controller_Helper_RuleMover_Strategy_MoveRuleIntoForeignSubmenu>());
		factory->ruleMover->addStrategy(factory->create<Controller_Helper_RuleMover_Strategy_MoveRuleOutOfProxyOnToplevel>());

		mainController->initAction();
		errorController->setApplicationStarted(true);

		application->applicationObject->run();
	} catch (Exception const& e) {
		logger->log(e, Logger::ERROR);
		return 1;
	}
}


#pragma once
void factoryDiagnosticsInit();
void factoryDiagnosticsPoll();
void factoryDiagnosticsSetSelection(int index, const char* name);
void factoryDiagnosticsSetWifiSetup(const char* state, int networks, int selected);
void factoryDiagnosticsSetBootWifi(const char* state);

// GOW-Port: muestreador externo de CPU por hilo para Windows (sin dependencias fuera del SDK).
//
// Cada intervalo suspende cada hilo del proceso, lee su RIP y lo reanuda; al terminar resuelve los símbolos y
// las líneas con DbgHelp (necesita el .pdb junto al ejecutable). Por cada hilo imprime su ocupación (muestras
// fuera de las funciones de espera del núcleo), las funciones, los archivos y las líneas más calientes.
//
// Compilar (Developer Command Prompt x64):
//   cl /nologo /O2 /EHsc /std:c++20 /utf-8 tools\rendimiento\muestreo.cpp winmm.lib
// Uso, con el juego ya lanzado o a punto de lanzarse (espera hasta 60 s a que aparezca el proceso):
//   muestreo.exe ps2EntryRunner.exe <espera_s> <duracion_s> [intervalo_ms [funcion]] > perfil.txt
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    DWORD findPid(const wchar_t *name)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W e{sizeof(e)};
        DWORD pid = 0;
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
            if (_wcsicmp(e.szExeFile, name) == 0)
            {
                pid = e.th32ProcessID;
                break;
            }
        CloseHandle(snap);
        return pid;
    }

    std::vector<DWORD> threadsOf(DWORD pid)
    {
        std::vector<DWORD> out;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 e{sizeof(e)};
        for (BOOL ok = Thread32First(snap, &e); ok; ok = Thread32Next(snap, &e))
            if (e.th32OwnerProcessID == pid)
                out.push_back(e.th32ThreadID);
        CloseHandle(snap);
        return out;
    }

    struct ThreadData
    {
        HANDLE handle = nullptr;
        uint64_t samples = 0;
        std::unordered_map<uint64_t, uint32_t> rips;
    };

    // Un hilo bloqueado aparece dentro de una de estas funciones de ntdll/win32u.
    bool isWait(const std::string &fn)
    {
        static const char *const waits[] = {"ZwWaitFor", "NtWaitFor", "NtDelayExecution", "ZwDelayExecution",
                                            "NtUserMsgWaitForMultipleObjectsEx", "NtUserWaitMessage", "ZwRemoveIoCompletion",
                                            "NtRemoveIoCompletion"};
        for (const char *w : waits)
            if (fn.rfind(w, 0) == 0)
                return true;
        return false;
    }

    template <class Map>
    std::vector<std::pair<std::string, uint64_t>> sorted(const Map &m)
    {
        std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        return v;
    }
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "uso: muestreo <exe> <espera_s> <duracion_s> [intervalo_ms [funcion]]\n");
        return 2;
    }
    const double wait = _wtof(argv[2]), duration = _wtof(argv[3]);
    std::string detail;
    if (argc > 5)
        for (const wchar_t *c = argv[5]; *c; ++c)
            detail.push_back(static_cast<char>(*c));
    const int interval = argc > 4 ? std::max(1, _wtoi(argv[4])) : 1;
    DWORD pid = 0;
    for (int i = 0; i < 600 && !pid; ++i)
    {
        pid = findPid(argv[1]);
        if (!pid)
            Sleep(100);
    }
    if (!pid)
    {
        std::fprintf(stderr, "no se encontro el proceso\n");
        return 1;
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    Sleep(static_cast<DWORD>(wait * 1000));

    // Sleep(1) puede durar 15,6 ms: temporizador de alta resolución.
    timeBeginPeriod(1);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    std::map<DWORD, ThreadData> threads;
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(duration * 1000);
    uint64_t ticks = 0;
    while (GetTickCount64() < end)
    {
        if (ticks % 200u == 0u)
            for (DWORD tid : threadsOf(pid))
                if (!threads.count(tid))
                    threads[tid].handle =
                        OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
        for (auto &[tid, t] : threads)
        {
            if (!t.handle || SuspendThread(t.handle) == DWORD(-1))
                continue;
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(t.handle, &ctx))
            {
                ++t.samples;
                ++t.rips[ctx.Rip];
            }
            ResumeThread(t.handle);
        }
        ++ticks;
        LARGE_INTEGER due;
        due.QuadPart = -10000LL * interval;
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(timer, INFINITE);
        else
            Sleep(interval);
    }
    timeEndPeriod(1);

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    std::unordered_map<uint64_t, std::string> names;
    const auto name = [&](uint64_t addr) -> const std::string & {
        if (const auto it = names.find(addr); it != names.end())
            return it->second;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
        auto *s = reinterpret_cast<SYMBOL_INFO *>(buf);
        s->SizeOfStruct = sizeof(SYMBOL_INFO);
        s->MaxNameLen = 511;
        DWORD64 disp = 0;
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "0x%llx", static_cast<unsigned long long>(addr));
        return names[addr] = SymFromAddr(proc, addr, &disp, s) ? std::string(s->Name) : std::string(tmp);
    };
    const auto lineOf = [&](uint64_t addr) -> std::string {
        IMAGEHLP_LINE64 line{sizeof(line)};
        DWORD disp = 0;
        if (!SymGetLineFromAddr64(proc, addr, &disp, &line))
            return name(addr) + ":?";
        const std::string file = line.FileName;
        const size_t slash = file.find_last_of("\\/");
        return file.substr(slash == std::string::npos ? 0 : slash + 1) + ":" + std::to_string(line.LineNumber);
    };

    std::vector<std::pair<DWORD, ThreadData *>> order;
    for (auto &[tid, t] : threads)
        if (t.samples)
            order.push_back({tid, &t});
    std::printf("ticks=%llu intervalo=%dms\n", static_cast<unsigned long long>(ticks), interval);
    std::vector<std::pair<std::string, double>> busyByThread;
    std::string report;
    for (auto &[tid, t] : order)
    {
        std::map<std::string, uint64_t> byFn, byLine, byFile;
        uint64_t waiting = 0;
        for (const auto &[rip, n] : t->rips)
        {
            const std::string &fn = name(rip);
            if (isWait(fn))
            {
                waiting += n;
                continue;
            }
            byFn[fn] += n;
            const std::string line = lineOf(rip);
            byLine[line] += n;
            byFile[line.substr(0, line.rfind(':'))] += n;
        }
        const double busy = 100.0 * static_cast<double>(t->samples - waiting) / static_cast<double>(t->samples);
        if (busy < 2.0)
            continue;
        std::wstring wdesc;
        PWSTR d = nullptr;
        if (SUCCEEDED(GetThreadDescription(t->handle, &d)) && d)
        {
            wdesc = d;
            LocalFree(d);
        }
        char label[160];
        std::snprintf(label, sizeof(label), "%lu %ls", tid, wdesc.c_str());
        busyByThread.push_back({label, busy});

        char head[256];
        std::snprintf(head, sizeof(head), "\n== hilo %s: ocupado %.1f %% (%llu muestras)\n", label, busy,
                      static_cast<unsigned long long>(t->samples));
        report += head;
        const auto section = [&](const char *title, const std::map<std::string, uint64_t> &m, size_t count) {
            report += std::string("  -- ") + title + " (% del tiempo del hilo)\n";
            const auto v = sorted(m);
            for (size_t i = 0; i < v.size() && i < count; ++i)
            {
                char row[512];
                std::snprintf(row, sizeof(row), "  %6.2f %%  %s\n",
                              100.0 * static_cast<double>(v[i].second) / static_cast<double>(t->samples),
                              v[i].first.c_str());
                report += row;
            }
        };
        section("funciones", byFn, 40);
        section("archivos", byFile, 20);
        section("lineas", byLine, 30);
        // Con un quinto argumento, histograma por instrucción de las funciones que contienen ese texto:
        // "direccion muestras funcion+desplazamiento" (para cruzar con dumpbin /disasm).
        if (!detail.empty())
        {
            std::vector<std::pair<uint64_t, uint32_t>> hits;
            for (const auto &[rip, n] : t->rips)
                if (name(rip).find(detail) != std::string::npos)
                    hits.push_back({rip, n});
            std::sort(hits.begin(), hits.end());
            if (!hits.empty())
                report += "  -- instrucciones de '" + detail + "'\n";
            for (const auto &[rip, n] : hits)
            {
                alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
                auto *s = reinterpret_cast<SYMBOL_INFO *>(buf);
                s->SizeOfStruct = sizeof(SYMBOL_INFO);
                s->MaxNameLen = 511;
                DWORD64 disp = 0;
                char row[700];
                if (SymFromAddr(proc, rip, &disp, s))
                    std::snprintf(row, sizeof(row), "  %llx %u %s+0x%llx rva=0x%llx\n", static_cast<unsigned long long>(rip), n,
                                  s->Name, static_cast<unsigned long long>(disp),
                                  static_cast<unsigned long long>(rip - s->ModBase));
                else
                    std::snprintf(row, sizeof(row), "  %llx %u\n", static_cast<unsigned long long>(rip), n);
                report += row;
            }
        }
    }
    std::sort(busyByThread.begin(), busyByThread.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    std::printf("\nOcupacion por hilo:\n");
    for (const auto &[label, busy] : busyByThread)
        std::printf("  %5.1f %%  %s\n", busy, label.c_str());
    std::fputs(report.c_str(), stdout);
    return 0;
}

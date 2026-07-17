#pragma once

// One-time, thread-safe initialization of Mesen's PROCESS-GLOBAL state (the "home folder" for its incidental
// config writes + the message-manager options). Both MesenNesSystem and MesenGbaSystem set the same values
// at construct; with the background render path building cores on multiple worker threads at once
// (RenderJobRegistry), those setters would race. std::call_once makes the setup run exactly once, whichever
// thread constructs the first Mesen core.
void mesenGlobalInit();

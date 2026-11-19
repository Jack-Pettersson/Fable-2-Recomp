// Sync trial: the minimal upstream TitleApp (from rexglue init's template).
#include <rex/app/title_app.h>

#include "generated/default/fable_2_init.h"

class Fable2App : public rex::app::TitleApp {
 public:
  Fable2App() : TitleApp("fable_2", kPPCImageConfig) {}
};

REX_TITLE_APP(Fable2App)

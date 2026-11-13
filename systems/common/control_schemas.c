#include "control_model.h"
#include <stdbool.h>
#include <string.h>
extern bool ps1_touch_descriptor(unsigned,bool,control_descriptor_t *);
extern bool gba_touch_descriptor(unsigned,bool,control_descriptor_t *);
const control_schema_t control_schemas[]={
 {"ps1",6,ps1_touch_descriptor},{"gba",6,gba_touch_descriptor}
};
const unsigned control_schema_count=sizeof(control_schemas)/sizeof(control_schemas[0]);

bool control_describe(const char *system,unsigned field,bool new_model,control_descriptor_t *out)
{
 for(unsigned i=0;i<control_schema_count;i++)if(!strcmp(system,control_schemas[i].system))
  return field<control_schemas[i].fields && control_schemas[i].describe(field,new_model,out);
 return false;
}

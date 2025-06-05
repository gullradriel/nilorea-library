/**\example ex_fluid_config.c
 *
 *  functions for fluid configuration
 *
 *\author Castagnier Mickael aka Gull Ra Driel
 *
 *\version 1.0
 *
 *\date 29/12/2021
 *
 */

#include "ex_fluid_config.h"
#include "nilorea/n_str.h"
#include "nilorea/n_fluids.h"
#include "nilorea/n_config_file.h"

int load_app_state(char* state_filename, size_t* WIDTH, size_t* HEIGHT, bool* fullscreen, char** bgmusic, double* drawFPS, double* logicFPS, N_FLUID* fluid, int* threaded) {
    __n_assert(state_filename, return FALSE);
    __n_assert(fluid, return FALSE);

    if (access(state_filename, F_OK) != 0) {
        n_log(LOG_INFO, "no app state %s to load !", state_filename);
        return FALSE;
    }

    /* N_STR *data = NULL ;
       data = file_to_nstr( state_filename );
       if( !data )
       {
       n_log( LOG_ERR , "Error reading file %s, defaults will be used" , state_filename );
       return FALSE;
       }

       cJSON *monitor_json = cJSON_Parse( _nstr( data ) );
       if (monitor_json == NULL)
       {
       const char *error_ptr = cJSON_GetErrorPtr();
       n_log( LOG_ERR , "%s: Error before: %s, defaults will be used", state_filename , _str( error_ptr ) );
       cJSON_Delete( monitor_json );
       free_nstr( &data );
       return FALSE ;
       }

       cJSON *value = NULL ;
       value = cJSON_GetObjectItemCaseSensitive( monitor_json, "width" );      if( cJSON_IsNumber( value ) ){ (*WIDTH)  = value -> valueint ; } else { n_log( LOG_ERR , "width is not a number"); }
       value = cJSON_GetObjectItemCaseSensitive( monitor_json, "height" );     if( cJSON_IsNumber( value ) ){ (*HEIGHT) = value -> valueint ; } else { n_log( LOG_ERR , "height is not a number"); }
       value = cJSON_GetObjectItemCaseSensitive( monitor_json, "fullscreen" ); if( cJSON_IsNumber( value ) ){ (*fullscreen) = value -> valueint ; } else { n_log( LOG_ERR , "fullscreen is not a number"); }
       value = cJSON_GetObjectItemCaseSensitive( monitor_json, "bg-music" );   if( cJSON_IsString( value ) ){ (*bgmusic) = strdup( value -> valuestring ); } else { n_log( LOG_ERR , "bg-music is not a string"); }
       value = cJSON_GetObjectItemCaseSensitive( monitor_json, "drawFPS" ); if( cJSON_IsNumber( value ) ){ (*drawFPS) = value -> valuedouble ; } else { n_log( LOG_ERR , "drawFPS is not a number"); }
       value = cJSON_GetObjectItemCaseSensitive( monitor_json, "logicFPS" ); if( cJSON_IsNumber( value ) ){ (*logicFPS) = value -> valuedouble ; } else { n_log( LOG_ERR , "logicFPS is not a number"); }

       cJSON_Delete(monitor_json);
       free_nstr( &data );
       */
    int errors = 0;
    CONFIG_FILE* config = load_config_file(state_filename, &errors);
    if (!config) {
        n_log(LOG_ERR, "Unable to load config file from %s", state_filename);
        return FALSE;
    }
    if (errors != 0) {
        n_log(LOG_ERR, "There were %d errors in %s. Check the logs !", errors, state_filename);
        return FALSE;
    }
    /* default section, only one should be allocated. Let's test it ! */
    int nb = get_nb_config_file_sections(config, "__DEFAULT__");
    for (int it = 0; it < nb; it++) {
        char* value = NULL;
        value = get_config_section_value(config, "__DEFAULT__", it, "width", 0);
        if (value) (*WIDTH) = atoi(value);
        value = get_config_section_value(config, "__DEFAULT__", it, "height", 0);
        if (value) (*HEIGHT) = atoi(value);
        value = get_config_section_value(config, "__DEFAULT__", it, "fullscreen", 0);
        if (value) (*fullscreen) = atoi(value);
        value = get_config_section_value(config, "__DEFAULT__", it, "bg-music", 0);
        if (value) (*bgmusic) = strdup(value);
        value = get_config_section_value(config, "__DEFAULT__", it, "drawFPS", 0);
        if (value) (*drawFPS) = strtold(value, NULL);
        value = get_config_section_value(config, "__DEFAULT__", it, "logicFPS", 0);
        if (value) (*logicFPS) = strtold(value, NULL);
        value = get_config_section_value(config, "__DEFAULT__", it, "numIters", 0);
        if (value) fluid->numIters = atoi(value);
        value = get_config_section_value(config, "__DEFAULT__", it, "density", 0);
        if (value) fluid->density = strtold(value, NULL);
        value = get_config_section_value(config, "__DEFAULT__", it, "gravity", 0);
        if (value) fluid->gravity = strtold(value, NULL);
        value = get_config_section_value(config, "__DEFAULT__", it, "overRelaxation", 0);
        if (value) fluid->overRelaxation = strtold(value, NULL);
        value = get_config_section_value(config, "__DEFAULT__", it, "fluid_production_percentage", 0);
        if (value) fluid->fluid_production_percentage = strtold(value, NULL);
        value = get_config_section_value(config, "__DEFAULT__", it, "cScale", 0);
        if (value) fluid->cScale = strtold(value, NULL);
        value = get_config_section_value(config, "__DEFAULT__", it, "threadedProcessing", 0);
        if (value) (*threaded) = atoi(value);
    }

    /*data = file_to_nstr( state_filename );
      if( !data )
      {
      n_log( LOG_ERR , "Error reading file %s, defaults will be used" , state_filename );
      return FALSE;
      }

      cJSON *monitor_json = cJSON_Parse( _nstr( data ) );
      if (monitor_json == NULL)
      {
      const char *error_ptr = cJSON_GetErrorPtr();
      n_log( LOG_ERR , "%s: Error before: %s, defaults will be used", state_filename , _str( error_ptr ) );
      cJSON_Delete( monitor_json );
      free_nstr( &data );
      return FALSE ;
      }

      cJSON *value = NULL ;
      value = cJSON_GetObjectItemCaseSensitive( monitor_json, "numIters" );       if( cJSON_IsNumber( value ) ){ fluid -> numIters = value -> valueint ; } else { n_log( LOG_ERR , "numIters is not a number"); }
      value = cJSON_GetObjectItemCaseSensitive( monitor_json, "density" );        if( cJSON_IsNumber( value ) ){ fluid -> density  = value -> valuedouble ; } else { n_log( LOG_ERR , "density is not a number"); }
      value = cJSON_GetObjectItemCaseSensitive( monitor_json, "gravity" );        if( cJSON_IsNumber( value ) ){ fluid -> gravity  = value -> valuedouble ; } else { n_log( LOG_ERR , "gravity is not a number"); }
      value = cJSON_GetObjectItemCaseSensitive( monitor_json, "overRelaxation" ); if( cJSON_IsNumber( value ) ){ fluid -> overRelaxation = value -> valuedouble ; } else { n_log( LOG_ERR , "overRelaxation is not a number"); }
      value = cJSON_GetObjectItemCaseSensitive( monitor_json, "fluid_production_percentage" ); if( cJSON_IsNumber( value ) ){ fluid -> fluid_production_percentage = value -> valuedouble ; }else { n_log( LOG_ERR , "fluid_production_percentage is not a number"); }
      value = cJSON_GetObjectItemCaseSensitive( monitor_json, "cScale" ); if( cJSON_IsNumber( value ) ){ fluid -> cScale = value -> valuedouble ; } else { n_log( LOG_ERR , "cScale is not a number"); }
      value = cJSON_GetObjectItemCaseSensitive( monitor_json, "threadedProcessing" ); if( cJSON_IsNumber( value ) ){ (*threaded) = value -> valueint ; } else { n_log( LOG_ERR , "threadedProcessing is not a number"); }

      cJSON_Delete(monitor_json);
      free_nstr( &data ); */

    destroy_config_file(&config);

    return TRUE;
}

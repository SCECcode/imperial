/**
 * @file ivlsu.c
 * @brief Main file for IMPERIAL-LSU library.
 * @author - SCEC 
 * @version 1.0
 *
 * @section DESCRIPTION
 *
 * Delivers LSU Imperial Valley Velocity Model
 *
 */

#include "ucvm_model_dtypes.h"
#include "ivlsu.h"

/** The config of the model */
char *ivlsu_config_string=NULL;
int ivlsu_config_sz=0;


/** Proj coordinate transformation objects. can go from geo <-> utm */
PJ *ivlsu_geo2utm = NULL;
char ivlsu_projstr[64];

static int to_utm(double lon, double lat, double *point_u, double *point_v) {
    PJ_COORD xyzSrc = proj_coord(lat, lon, 0.0, HUGE_VAL);
    PJ_COORD xyzDest = proj_trans(ivlsu_geo2utm, PJ_FWD, xyzSrc);
    int err = proj_context_errno(PJ_DEFAULT_CTX);
    if (err) {
       fprintf(stderr, "Error occurred while transforming latitude=%.4f, longitude=%.4f to UTM.\n",
              lat, lon);
        fprintf(stderr, "Proj error: %s\n", proj_context_errno_string(PJ_DEFAULT_CTX, err));
        return UCVM_CODE_ERROR;
    }
    *point_u = xyzDest.xyzt.x;
    *point_v = xyzDest.xyzt.y;
    return err;
}

static int to_geo(double point_u, double point_v, double *lon, double *lat) {
    PJ_COORD xyzSrc;
    xyzSrc.xyzt.x=point_u;
    xyzSrc.xyzt.y=point_v;
    PJ_COORD xyzDest = proj_trans(ivlsu_geo2utm, PJ_INV, xyzSrc);

    int err = proj_context_errno(PJ_DEFAULT_CTX);
    if (err) {
       fprintf(stderr, "Error occurred while transforming u=%.4f, v=%.4f to Geo.\n",
              point_u, point_v);
        fprintf(stderr, "Proj error: %s\n", proj_context_errno_string(PJ_DEFAULT_CTX, err));
        return UCVM_CODE_ERROR;
    }
    *lon=xyzDest.lp.lam;
    *lat=xyzDest.lp.phi;
    return err;
}



/**
 * Initializes the IMPERIAL plugin model within the UCVM framework. In order to initialize
 * the model, we must provide the UCVM install path and optionally a place in memory
 * where the model already exists.
 *
 * @param dir The directory in which UCVM has been installed.
 * @param label A unique identifier for the velocity model.
 * @return Success or failure, if initialization was successful.
 */
int ivlsu_init(const char *dir, const char *label) {
    int tempVal = 0;
    char configbuf[512];
    double north_height_m = 0, east_width_m = 0, rotation_angle = 0;

    // Initialize variables.
    ivlsu_configuration = calloc(1, sizeof(ivlsu_configuration_t));
    ivlsu_velocity_model = calloc(1, sizeof(ivlsu_model_t));

    ivlsu_config_string = calloc(IVLSU_CONFIG_MAX, sizeof(char));
    ivlsu_config_string[0]='\0';
    ivlsu_config_sz=0;

    // Configuration file location.
    sprintf(configbuf, "%s/model/%s/data/config", dir, label);

    // Read the configuration file.
    if (ivlsu_read_configuration(configbuf, ivlsu_configuration) != SUCCESS) {
        tempVal = FAIL;
        ivlsu_print_error("No configuration file was found to read from.");
        return FAIL;
    }

    // Set up the data directory.
    sprintf(ivlsu_data_directory, "%s/model/%s/data/%s", dir, label, ivlsu_configuration->model_dir);

    // Can we allocate the model, or parts of it, to memory. If so, we do.
    tempVal = ivlsu_try_reading_model(ivlsu_velocity_model);

    if (tempVal == SUCCESS) {
        fprintf(stderr, "WARNING: Could not load model into memory. Reading the model from the\n");
        fprintf(stderr, "hard disk may result in slow performance.");
        } else if (tempVal == FAIL) {
            ivlsu_print_error("No model file was found to read from.");
            return FAIL;
    }

    // In order to simplify our calculations in the query, we want to rotate the box so that the bottom-left
    // corner is at (0m,0m). Our box's height is total_height_m and total_width_m. We then rotate the
    // point so that is is somewhere between (0,0) and (total_width_m, total_height_m). How far along
    // the X and Y axis determines which grid points we use for the interpolation routine.

    // Calculate the rotation angle of the box.
    north_height_m = ivlsu_configuration->top_left_corner_n - ivlsu_configuration->bottom_left_corner_n;
    east_width_m = ivlsu_configuration->top_left_corner_e - ivlsu_configuration->bottom_left_corner_e;
    
    // Rotation angle. Cos, sin, and tan are expensive computationally, so calculate once.
    rotation_angle = atan(east_width_m / north_height_m);

    ivlsu_cos_rotation_angle = cos(rotation_angle);
    ivlsu_sin_rotation_angle = sin(rotation_angle);

    ivlsu_total_height_m = sqrt(pow(ivlsu_configuration->top_left_corner_n - ivlsu_configuration->bottom_left_corner_n, 2.0f) +
                          pow(ivlsu_configuration->top_left_corner_e - ivlsu_configuration->bottom_left_corner_e, 2.0f));
    ivlsu_total_width_m  = sqrt(pow(ivlsu_configuration->top_right_corner_n - ivlsu_configuration->top_left_corner_n, 2.0f) +
                          pow(ivlsu_configuration->top_right_corner_e - ivlsu_configuration->top_left_corner_e, 2.0f));

    snprintf(ivlsu_projstr, 64, "+proj=utm +ellps=clrk66 +zone=%d +datum=NAD27 +units=m +no_defs", ivlsu_configuration->utm_zone);
    if (!(ivlsu_geo2utm = proj_create_crs_to_crs(PJ_DEFAULT_CTX, "EPSG:4326", ivlsu_projstr, NULL))) {
        ivlsu_print_error("Could not set up Proj transformation from EPSG:4326 to UTM.");
        ivlsu_print_error((char  *)proj_context_errno_string(PJ_DEFAULT_CTX, proj_context_errno(PJ_DEFAULT_CTX)));
        return (UCVM_CODE_ERROR);
    }

    /* setup config_string */
    sprintf(ivlsu_config_string,"config = %s\n",configbuf);
    ivlsu_config_sz=1;

    // Let everyone know that we are initialized and ready for business.
    ivlsu_is_initialized = 1;

    return SUCCESS;
}

/**
 * Queries IMPERIAL at the given points and returns the data that it finds.
 *
 * @param points The points at which the queries will be made.
 * @param data The data that will be returned (Vp, Vs, density, Qs, and/or Qp).
 * @param numpoints The total number of points to query.
 * @return SUCCESS or FAIL.
 */
int ivlsu_query(ivlsu_point_t *points, ivlsu_properties_t *data, int numpoints) {
    int i = 0;

    int load_x_coord = 0, load_y_coord = 0, load_z_coord = 0;
    double x_percent = 0, y_percent = 0, z_percent = 0;

    ivlsu_properties_t surrounding_points[8];
    int zone = ivlsu_configuration->utm_zone;

    double lon_e, lat_n;

    double point_u = 0, point_v = 0;
    double point_x = 0, point_y = 0;


    int longlat2utm = 0;
    double point_utm_e = 0, point_utm_n = 0;

    double delta_lon = (ivlsu_configuration->top_right_corner_e - ivlsu_configuration->bottom_left_corner_e)/(ivlsu_configuration->nx - 1);
    double delta_lat = (ivlsu_configuration->top_right_corner_n - ivlsu_configuration->bottom_left_corner_n)/(ivlsu_configuration->ny - 1);

    for (i = 0; i < numpoints; i++) {

        // We need to be below the surface to service this query.
        if (points[i].depth < 0) {
            data[i].vp = -1;
            data[i].vs = -1;
            data[i].rho = -1;
            data[i].qp = -1;
            data[i].qs = -1;
            continue;
        }

        // lon,lat,u,v                       
        to_utm(points[i].longitude, points[i].latitude, &point_u, &point_v);

//fprintf(stderr,"XXX point_u is %lf \n", point_u);
//fprintf(stderr,"XXX point_v is %lf \n", point_v);

        // Point within rectangle.
        point_u -= ivlsu_configuration->bottom_left_corner_e;
        point_v -= ivlsu_configuration->bottom_left_corner_n;

        // We need to rotate that point, the number of degrees we calculated above.
        point_x = ivlsu_cos_rotation_angle * point_u - ivlsu_sin_rotation_angle * point_v;
        point_y = ivlsu_sin_rotation_angle * point_u + ivlsu_cos_rotation_angle * point_v;

//fprintf(stderr,"XXX point_x is %lf \n", point_x);
//fprintf(stderr,"XXX point_y is %lf \n", point_y);


// Which point base point does that correspond to?
        load_y_coord = (int)(round(point_y/delta_lat));
        load_x_coord = (int)(round(point_x/delta_lon));
        load_z_coord = (int)(points[i].depth/1000);

//fprintf(stderr,"XXX USING: load_y_coord %d load_x_coord %d load_z_coord %d\n", load_y_coord, load_x_coord, load_z_coord);

        // Are we outside the model's X and Y and Z boundaries?
        if (points[i].depth > ivlsu_configuration->depth || load_x_coord > ivlsu_configuration->nx -1  || load_y_coord > ivlsu_configuration->ny -1 || load_x_coord < 0 || load_y_coord < 0 || load_z_coord < 0) {
            data[i].vp = -1;
            data[i].vs = -1;
            data[i].rho = -1;
            continue;
        }

// always in interpolaton mode
        double x_interval=(ivlsu_configuration->nx > 1) ?
                     ivlsu_total_width_m / (ivlsu_configuration->nx-1):ivlsu_total_width_m;
        double y_interval=(ivlsu_configuration->ny > 1) ?
                     ivlsu_total_height_m / (ivlsu_configuration->ny-1):ivlsu_total_height_m;

        // Get the X, Y, and Z percentages for the bilinear or trilinear interpolation below.
        x_percent = fmod(point_u, x_interval) / x_interval;
        y_percent = fmod(point_v, y_interval) / y_interval;
        z_percent = fmod(points[i].depth, ivlsu_configuration->depth_interval) / ivlsu_configuration->depth_interval;

//fprintf(stderr,"XXX x_percent %lf y_percent %lf z_percent %lf\n",x_percent, y_percent, z_percent);

        if (load_z_coord == 0 && z_percent == 0) {
            // We're below the model boundaries. Bilinearly interpolate the bottom plane and use that value.
            load_z_coord = 0;
            if(ivlsu_configuration->interpolation) {

              // Get the four properties.
              ivlsu_read_properties(load_x_coord,     load_y_coord,     load_z_coord,     &(surrounding_points[0]));    // Orgin.
              ivlsu_read_properties(load_x_coord + 1, load_y_coord,     load_z_coord,     &(surrounding_points[1]));    // Orgin + 1x
              ivlsu_read_properties(load_x_coord,     load_y_coord + 1, load_z_coord,     &(surrounding_points[2]));    // Orgin + 1y
              ivlsu_read_properties(load_x_coord + 1, load_y_coord + 1, load_z_coord,     &(surrounding_points[3]));    // Orgin + x + y, forms top plane.

              ivlsu_bilinear_interpolation(x_percent, y_percent, surrounding_points, &(data[i]));
              } else {
                 ivlsu_read_properties(load_x_coord,     load_y_coord,     load_z_coord,     &(data[i]));        // Orgin.
            }
            } else {
                if( ivlsu_configuration->interpolation) {
                // Read all the surrounding point properties.
                  ivlsu_read_properties(load_x_coord,     load_y_coord,     load_z_coord,     &(surrounding_points[0]));    // Orgin.
                  ivlsu_read_properties(load_x_coord + 1, load_y_coord,     load_z_coord,     &(surrounding_points[1]));    // Orgin + 1x
                  ivlsu_read_properties(load_x_coord,     load_y_coord + 1, load_z_coord,     &(surrounding_points[2]));    // Orgin + 1y
                  ivlsu_read_properties(load_x_coord + 1, load_y_coord + 1, load_z_coord,     &(surrounding_points[3]));    // Orgin + x + y, forms top plane.
                  ivlsu_read_properties(load_x_coord,     load_y_coord,     load_z_coord - 1, &(surrounding_points[4]));    // Bottom plane origin
                  ivlsu_read_properties(load_x_coord + 1, load_y_coord,     load_z_coord - 1, &(surrounding_points[5]));    // +1x
                  ivlsu_read_properties(load_x_coord,     load_y_coord + 1, load_z_coord - 1, &(surrounding_points[6]));    // +1y
                  ivlsu_read_properties(load_x_coord + 1, load_y_coord + 1, load_z_coord - 1, &(surrounding_points[7]));    // +x +y, forms bottom plane.
  
                  ivlsu_trilinear_interpolation(x_percent, y_percent, z_percent, surrounding_points, &(data[i]));
             } else { // no interpolation, data as it is
                ivlsu_read_properties(load_x_coord,     load_y_coord,     load_z_coord,     &(data[i]));        // Orgin.
          }
        }

        data[i].rho = ivlsu_calculate_density(data[i].vp);
        data[i].vs = ivlsu_calculate_vs(data[i].vp);
    }

    return SUCCESS;
}

/**
 * Retrieves the material properties (whatever is available) for the given data point, expressed
 * in x, y, and z co-ordinates.
 *
 * @param x The x coordinate of the data point.
 * @param y The y coordinate of the data point.
 * @param z The z coordinate of the data point.
 * @param data The properties struct to which the material properties will be written.
 */
void ivlsu_read_properties(int x, int y, int z, ivlsu_properties_t *data) {

//fprintf(stderr,"XXX  reading x%d y%d z%d\n",x,y,z);

    // Set everything to -1 to indicate not found.
    data->vp = -1;
    data->vs = -1;
    data->rho = -1;

    float *ptr = NULL;
    FILE *fp = NULL;

    int location = z * (ivlsu_configuration->nx * ivlsu_configuration->ny) + (y * ivlsu_configuration->nx) + x;

//printf(">>> LOCATION ivlsu %d\n",location);
    // Check our loaded components of the model.
    if (ivlsu_velocity_model->vp_status == 2) {
        // Read from memory.
        ptr = (float *)ivlsu_velocity_model->vp;
        data->vp = ptr[location];
    } else if (ivlsu_velocity_model->vp_status == 1) {
        // Read from file.
        fseek(fp, location * sizeof(float), SEEK_SET);
        fread(&(data->vp), sizeof(float), 1, fp);
    }
}

/**
 * Trilinearly interpolates given a x percentage, y percentage, z percentage and a cube of
 * data properties in top origin format (top plane first, bottom plane second).
 *
 * @param x_percent X percentage
 * @param y_percent Y percentage
 * @param z_percent Z percentage
 * @param eight_points Eight surrounding data properties
 * @param ret_properties Returned data properties
 */
void ivlsu_trilinear_interpolation(double x_percent, double y_percent, double z_percent,
                             ivlsu_properties_t *eight_points, ivlsu_properties_t *ret_properties) {
    ivlsu_properties_t *temp_array = calloc(2, sizeof(ivlsu_properties_t));
    ivlsu_properties_t *four_points = eight_points;

    ivlsu_bilinear_interpolation(x_percent, y_percent, four_points, &temp_array[0]);

    // Now advance the pointer four "ivlsu_properties_t" spaces.
    four_points += 4;

    // Another interpolation.
    ivlsu_bilinear_interpolation(x_percent, y_percent, four_points, &temp_array[1]);

    // Now linearly interpolate between the two.
    ivlsu_linear_interpolation(z_percent, &temp_array[0], &temp_array[1], ret_properties);

    free(temp_array);
}

/**
 * Bilinearly interpolates given a x percentage, y percentage, and a plane of data properties in
 * origin, bottom-right, top-left, top-right format.
 *
 * @param x_percent X percentage.
 * @param y_percent Y percentage.
 * @param four_points Data property plane.
 * @param ret_properties Returned data properties.
 */
void ivlsu_bilinear_interpolation(double x_percent, double y_percent, ivlsu_properties_t *four_points, ivlsu_properties_t *ret_properties) {

    ivlsu_properties_t *temp_array = calloc(2, sizeof(ivlsu_properties_t));

    ivlsu_linear_interpolation(x_percent, &four_points[0], &four_points[1], &temp_array[0]);
    ivlsu_linear_interpolation(x_percent, &four_points[2], &four_points[3], &temp_array[1]);
    ivlsu_linear_interpolation(y_percent, &temp_array[0], &temp_array[1], ret_properties);

    free(temp_array);
}

/**
 * Linearly interpolates given a percentage from x0 to x1, a data point at x0, and a data point at x1.
 *
 * @param percent Percent of the way from x0 to x1 (from 0 to 1 interval).
 * @param x0 Data point at x0.
 * @param x1 Data point at x1.
 * @param ret_properties Resulting data properties.
 */
void ivlsu_linear_interpolation(double percent, ivlsu_properties_t *x0, ivlsu_properties_t *x1, ivlsu_properties_t *ret_properties) {

    ret_properties->vp  = (1 - percent) * x0->vp  + percent * x1->vp;
    ret_properties->vs  = (1 - percent) * x0->vs  + percent * x1->vs;
    ret_properties->rho = (1 - percent) * x0->rho + percent * x1->rho;
}

/**
 * Called when the model is being discarded. Free all variables.
 *
 * @return SUCCESS
 */
int ivlsu_finalize() {
    proj_destroy(ivlsu_geo2utm);
    ivlsu_geo2utm = NULL;

    if (ivlsu_velocity_model->vp) free(ivlsu_velocity_model->vp);
    free(ivlsu_configuration);

    return SUCCESS;
}

/**
 * Returns the version information.
 *
 * @param ver Version string to return.
 * @param len Maximum length of buffer.
 * @return Zero
 */
int ivlsu_version(char *ver, int len)
{
  int verlen;
  verlen = strlen(ivlsu_version_string);
  if (verlen > len - 1) {
    verlen = len - 1;
  }
  memset(ver, 0, len);
  strncpy(ver, ivlsu_version_string, verlen);
  return 0;
}

/**
 * Returns the model config information.
 *
 * @param key Config key string to return.
 * @return Zero
 */
int ivlsu_config(char **config, int *sz)
{
  int len=strlen(ivlsu_config_string);
  if(len > 0) {
    *config=ivlsu_config_string;
    *sz=ivlsu_config_sz;
    return SUCCESS;
  }
  return FAIL;
}

/**
 * Reads the configuration file describing the various properties of CVM-S5 and populates
 * the configuration struct. This assumes configuration has been "calloc'ed" and validates
 * that each value is not zero at the end.
 *
 * @param file The configuration file location on disk to read.
 * @param config The configuration struct to which the data should be written.
 * @return Success or failure, depending on if file was read successfully.
 */
int ivlsu_read_configuration(char *file, ivlsu_configuration_t *config) {
    FILE *fp = fopen(file, "r");
    char key[40];
    char value[80];
    char line_holder[128];

    // If our file pointer is null, an error has occurred. Return fail.
    if (fp == NULL) {
        ivlsu_print_error("Could not open the configuration file.");
        return FAIL;
    }

    // Read the lines in the configuration file.
    while (fgets(line_holder, sizeof(line_holder), fp) != NULL) {
        if (line_holder[0] != '#' && line_holder[0] != ' ' && line_holder[0] != '\n') {
            sscanf(line_holder, "%s = %s", key, value);

            // Which variable are we editing?
            if (strcmp(key, "utm_zone") == 0)
                  config->utm_zone = atoi(value);
            if (strcmp(key, "model_dir") == 0)
                sprintf(config->model_dir, "%s", value);
            if (strcmp(key, "nx") == 0)
                  config->nx = atoi(value);
            if (strcmp(key, "ny") == 0)
                   config->ny = atoi(value);
            if (strcmp(key, "nz") == 0)
                   config->nz = atoi(value);
            if (strcmp(key, "depth") == 0)
                   config->depth = atof(value);
            if (strcmp(key, "top_left_corner_e") == 0)
                config->top_left_corner_e = atof(value);
            if (strcmp(key, "top_left_corner_n") == 0)
                 config->top_left_corner_n = atof(value);
            if (strcmp(key, "top_right_corner_e") == 0)
                config->top_right_corner_e = atof(value);
            if (strcmp(key, "top_right_corner_n") == 0)
                config->top_right_corner_n = atof(value);
            if (strcmp(key, "bottom_left_corner_e") == 0)
                config->bottom_left_corner_e = atof(value);
            if (strcmp(key, "bottom_left_corner_n") == 0)
                config->bottom_left_corner_n = atof(value);
            if (strcmp(key, "bottom_right_corner_e") == 0)
                config->bottom_right_corner_e = atof(value);
            if (strcmp(key, "bottom_right_corner_n") == 0)
                config->bottom_right_corner_n = atof(value);
            if (strcmp(key, "depth_interval") == 0)
                config->depth_interval = atof(value);
            if (strcmp(key, "interpolation") == 0) {
                                if (strcmp(value, "on") == 0) {
                                     config->interpolation = 1;
                                     } else {
                                          config->interpolation = 0;
                                }
                        };

        }
    }

    // Have we set up all configuration parameters?
    if (config->utm_zone == 0 || config->nx == 0 || config->ny == 0 || config->nz == 0 || config->model_dir[0] == '\0' ||
        config->top_left_corner_e == 0 || config->top_left_corner_n == 0 || config->top_right_corner_e == 0 ||
        config->top_right_corner_n == 0 || config->bottom_left_corner_e == 0 || config->bottom_left_corner_n == 0 ||
        config->bottom_right_corner_e == 0 || config->bottom_right_corner_n == 0 || config->depth == 0 ||
        config->depth_interval == 0 ) {
        ivlsu_print_error("One configuration parameter not specified. Please check your configuration file.");
        return FAIL;
    }

    fclose(fp);

    return SUCCESS;
}

/**
 * Calculates the density based off of Vp. Base on Brocher's formulae
 *
 * @param vp 
 * @return Density, in g/m^3.
 * [eqn. 6] r (g/cm3) = 1.6612Vp – 0.4721Vp2 + 0.0671Vp3 – 0.0043Vp4 + 0.000106Vp5.
 * Equation 6 is the “Nafe-Drake curve” (Ludwig et al., 1970).
 * start with vp in km 
 */
double ivlsu_calculate_density(double vp) {
     double retVal ;

     vp = vp * 0.001;
     double t1 = (vp * 1.6612);
     double t2 = ((vp * vp ) * 0.4721);
     double t3 = ((vp * vp * vp) * 0.0671);
     double t4 = ((vp * vp * vp * vp) * 0.0043);
     double t5 = ((vp * vp * vp * vp * vp) * 0.000106);
     retVal = t1 - t2 + t3 - t4 + t5;
     if (retVal < 1.0) {
       retVal = 1.0;
     }
     retVal = retVal * 1000.0;
     return retVal;
}

/**
 * Calculates the vs based off of Vp. Base on Brocher's formulae
 *
 * https://pubs.usgs.gov/of/2005/1317/of2005-1317.pdf
 *
 * @param vp
 * @return Vs, in km.
 * Vs derived from Vp, Brocher (2005) eqn 1.
 * [eqn. 1] Vs (km/s) = 0.7858 – 1.2344Vp + 0.7949Vp2 – 0.1238Vp3 + 0.0064Vp4.
 * Equation 1 is valid for 1.5 < Vp < 8 km/s.
 */
double ivlsu_calculate_vs(double vp) {
     double retVal ;

     vp = vp * 0.001;
     double t1= (vp * 1.2344);
     double t2= ((vp * vp)* 0.7949);
     double t3= ((vp * vp * vp) * 0.1238);
     double t4= ((vp * vp * vp * vp) * 0.0064);
     retVal = 0.7858 - t1 + t2 - t3 + t4;
     retVal = retVal * 1000.0;
     return retVal;
}


/**
 * Prints the error string provided.
 *
 * @param err The error string to print out to stderr.
 */
void ivlsu_print_error(char *err) {
    fprintf(stderr, "An error has occurred while executing IMPERIAL. The error was:\n\n");
    fprintf(stderr, "%s", err);
    fprintf(stderr, "\n\nPlease contact software@scec.org and describe both the error and a bit\n");
    fprintf(stderr, "about the computer you are running IMPERIAL on (Linux, Mac, etc.).\n");
}

/**
 * Tries to read the model into memory.
 *
 * @param model The model parameter struct which will hold the pointers to the data either on disk or in memory.
 * @return 2 if all files are read to memory, SUCCESS if file is found but at least 1
 * is not in memory, FAIL if no file found.
 */
int ivlsu_try_reading_model(ivlsu_model_t *model) {
    double base_malloc = ivlsu_configuration->nx * ivlsu_configuration->ny * ivlsu_configuration->nz * sizeof(float);
    int file_count = 0;
    int all_read_to_memory = 1;
    char current_file[128];
    FILE *fp;

    // Let's see what data we actually have.
    sprintf(current_file, "%s/vp.dat", ivlsu_data_directory);
    if (access(current_file, R_OK) == 0) {
        model->vp = malloc(base_malloc);
        if (model->vp != NULL) {
            // Read the model in.
            fp = fopen(current_file, "rb");
            fread(model->vp, 1, base_malloc, fp);
            fclose(fp);
            model->vp_status = 2;
        } else {
            all_read_to_memory = 0;
            model->vp = fopen(current_file, "rb");
            model->vp_status = 1;
        }
        file_count++;
    }

    if (file_count == 0)
        return FAIL;
    else if (file_count > 0 && all_read_to_memory == 0)
        return SUCCESS;
    else
        return 2;
}

// The following functions are for dynamic library mode. If we are compiling
// a static library, these functions must be disabled to avoid conflicts.
#ifdef DYNAMIC_LIBRARY

/**
 * Init function loaded and called by the UCVM library. Calls ivlsu_init.
 *
 * @param dir The directory in which UCVM is installed.
 * @return Success or failure.
 */
int model_init(const char *dir, const char *label) {
    return ivlsu_init(dir, label);
}

/**
 * Query function loaded and called by the UCVM library. Calls ivlsu_query.
 *
 * @param points The basic_point_t array containing the points.
 * @param data The basic_properties_t array containing the material properties returned.
 * @param numpoints The number of points in the array.
 * @return Success or fail.
 */
int model_query(ivlsu_point_t *points, ivlsu_properties_t *data, int numpoints) {
    return ivlsu_query(points, data, numpoints);
}

/**
 * Finalize function loaded and called by the UCVM library. Calls ivlsu_finalize.
 *
 * @return Success
 */
int model_finalize() {
    return ivlsu_finalize();
}

/**
 * Version function loaded and called by the UCVM library. Calls ivlsu_version.
 *
 * @param ver Version string to return.
 * @param len Maximum length of buffer.
 * @return Zero
 */
int model_version(char *ver, int len) {
    return ivlsu_version(ver, len);
}

/**
 * Version function loaded and called by the UCVM library. Calls ivlsu_config.
 *
 * @param config Config string to return.
 * @param sz length of config terms.
 * @return Zero
 */
int model_config(char **config, int *sz) {
        return ivlsu_config(config, sz);
}

int (*get_model_init())(const char *, const char *) {
        return &ivlsu_init;
}
int (*get_model_query())(ivlsu_point_t *, ivlsu_properties_t *, int) {
         return &ivlsu_query;
}
int (*get_model_finalize())() {
         return &ivlsu_finalize;
}
int (*get_model_version())(char *, int) {
         return &ivlsu_version;
}
int (*get_model_config())(char **, int*) {
         return &ivlsu_config;
}



#endif

/**
 * @file ivlsu.h
 * @brief Main header file for LSU Imperial Valley library.
 * @author - SCEC 
 * @version 1.0
 *
 * Delivers LSU Imperial Valley Velocity Model
 *
 */

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "proj.h"

// Constants
#ifndef M_PI
	/** Defines pi */
	#define M_PI 3.14159265358979323846
#endif

/** Defines a return value of success */
#define SUCCESS 0
/** Defines a return value of failure */
#define FAIL 1
/** Defines a return value of NA from model */
#define NA -1 

/* config string */
#define IVLSU_CONFIG_MAX 1000

// Structures
/** Defines a point (latitude, longitude, and depth) in WGS84 format */
typedef struct ivlsu_point_t {
	/** Longitude member of the point */
	double longitude;
	/** Latitude member of the point */
	double latitude;
	/** Depth member of the point */
	double depth;
} ivlsu_point_t;

/** Defines the material properties this model will retrieve. */
typedef struct ivlsu_properties_t {
	/** P-wave velocity in meters per second */
	double vp;
	/** S-wave velocity in meters per second */
	double vs;
	/** Density in g/m^3 */
	double rho;
        /** NOT USED from basic_property_t */
        double qp;
        /** NOT USED from basic_property_t */
        double qs;

} ivlsu_properties_t;

/** The IMPERIAL configuration structure. */
typedef struct ivlsu_configuration_t {
	/** The zone of UTM projection */
	int utm_zone;
	/** The model directory */
	char model_dir[128];
	/** Number of x points */
	int nx;
	/** Number of y points */
	int ny;
	/** Number of z points */
	int nz;
	/** Depth in meters */
	double depth;
	/** Top left corner easting */
	double top_left_corner_e;
	/** Top left corner northing */
	double top_left_corner_n;
	/** Top right corner easting */
	double top_right_corner_e;
	/** Top right corner northing */
	double top_right_corner_n;
	/** Bottom left corner easting */
	double bottom_left_corner_e;
	/** Bottom left corner northing */
	double bottom_left_corner_n;
	/** Bottom right corner easting */
	double bottom_right_corner_e;
	/** Bottom right corner northing */
	double bottom_right_corner_n;
	/** Z interval for the data */
	double depth_interval;
	double p5;
        /** Bilinear or Trilinear Interpolation on or off (1 or 0) */
        int interpolation;

} ivlsu_configuration_t;

/** The model structure which points to available portions of the model. */
typedef struct ivlsu_model_t {
	/** A pointer to the Vp data either in memory or disk. Null if does not exist. */
	void *vp;
	/** Vp status: 0 = not found, 1 = found and not in memory, 2 = found and in memory */
	int vp_status;
} ivlsu_model_t;

// Constants
/** The version of the model. */
const char *ivlsu_version_string = "IMPERIAL";

// Variables
/** Set to 1 when the model is ready for query. */
int ivlsu_is_initialized = 0;

/** Location of the binary data files. */
char ivlsu_data_directory[128];

/** Configuration parameters. */
ivlsu_configuration_t *ivlsu_configuration;
/** Holds pointers to the velocity model data OR indicates it can be read from file. */
ivlsu_model_t *ivlsu_velocity_model;

/** The cosine of the rotation angle used to rotate the box and point around the bottom-left corner. */
double ivlsu_cos_rotation_angle = 0;
/** The sine of the rotation angle used to rotate the box and point around the bottom-left corner. */
double ivlsu_sin_rotation_angle = 0;

/** The height of this model's region, in meters. */
double ivlsu_total_height_m = 0;
/** The width of this model's region, in meters. */
double ivlsu_total_width_m = 0;

// UCVM API Required Functions

#ifdef DYNAMIC_LIBRARY

/** Initializes the model */
int model_init(const char *dir, const char *label);
/** Cleans up the model (frees memory, etc.) */
int model_finalize();
/** Returns version information */
int model_version(char *ver, int len);
/** Queries the model */
int model_query(ivlsu_point_t *points, ivlsu_properties_t *data, int numpts);

#endif

// IMPERIAL Related Functions

/** Initializes the model */
extern int ivlsu_init(const char *dir, const char *label);
/** Cleans up the model (frees memory, etc.) */
extern int ivlsu_finalize();
/** Returns version information */
extern int ivlsu_version(char *ver, int len);
/** Queries the model */
extern int ivlsu_query(ivlsu_point_t *points, ivlsu_properties_t *data, int numpts);

// Non-UCVM Helper Functions
/** Reads the configuration file. */
extern int ivlsu_read_configuration(char *file, ivlsu_configuration_t *config);
extern void ivlsu_print_error(char *err);
/** Retrieves the value at a specified grid point in the model. */
extern void ivlsu_read_properties(int x, int y, int z, ivlsu_properties_t *data);
/** Attempts to malloc the model size in memory and read it in. */
extern int ivlsu_try_reading_model(ivlsu_model_t *model);
/** Calculates density from Vp. */
extern double ivlsu_calculate_density(double vp);
/** Calculates Vs from Vp. */
extern double ivlsu_calculate_vs(double vp);

// Interpolation Functions
/** Linearly interpolates two ivlsu_properties_t structures */
extern void ivlsu_linear_interpolation(double percent, ivlsu_properties_t *x0, ivlsu_properties_t *x1, ivlsu_properties_t *ret_properties);
/** Bilinearly interpolates the properties. */
extern void ivlsu_bilinear_interpolation(double x_percent, double y_percent, ivlsu_properties_t *four_points, ivlsu_properties_t *ret_properties);
/** Trilinearly interpolates the properties. */
extern void ivlsu_trilinear_interpolation(double x_percent, double y_percent, double z_percent, ivlsu_properties_t *eight_points,
							 ivlsu_properties_t *ret_properties);

/*  MiniDLNA media server
 *  Copyright (C) 2008  Justin Maggard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>
#include <libgen.h>
#include <sys/stat.h>

#include <sqlite3.h>

#include "upnpglobalvars.h"
#include "metadata.h"
#include "utils.h"
#include "sql.h"
#include "scanner.h"

struct virtual_item
{
	int  objectID;
	char parentID[64];
	char name[256];
};

int
is_video(const char * file)
{
	return (ends_with(file, ".mpg") || ends_with(file, ".mpeg")  ||
		ends_with(file, ".asf") || ends_with(file, ".wmv")   ||
		ends_with(file, ".mp4") || ends_with(file, ".m4v")   ||
		ends_with(file, ".mts") || ends_with(file, ".m2ts")  ||
		ends_with(file, ".vob") || ends_with(file, ".ts")    ||
		ends_with(file, ".avi") || ends_with(file, ".xvid"));
}

int
is_audio(const char * file)
{
	return (ends_with(file, ".mp3") || ends_with(file, ".flac") ||
		ends_with(file, ".fla") || ends_with(file, ".flc")  ||
		ends_with(file, ".m4a") || ends_with(file, ".aac"));
}

int
is_image(const char * file)
{
	return (ends_with(file, ".jpg") || ends_with(file, ".jpeg"));
}

sqlite_int64
get_next_available_id(const char * table, const char * parentID)
{
		char * sql;
		char **result;
		int ret, rows;
		sqlite_int64 objectID = 0;

		asprintf(&sql, "SELECT OBJECT_ID, max(ID) from %s where PARENT_ID = '%s'", table, parentID);
		ret = sql_get_table(db, sql, &result, &rows, NULL);
		if( result[2] )
		{
			objectID = strtoll(rindex(result[2], '$')+1, NULL, 16) + 1;
		}
		sqlite3_free_table(result);
		free(sql);

		return objectID;
}

long long int
insert_container(const char * item, const char * rootParent, const char * refID, const char *class,
                 const char *artist, const char *genre, const char *album_art, const char *art_dlna_pn)
{
	char **result;
	char *sql;
	int cols, rows, ret;
	int parentID = 0, objectID = 0;
	sqlite_int64 detailID;

	sql = sqlite3_mprintf("SELECT OBJECT_ID from OBJECTS where OBJECT_ID glob '%s$*' and NAME = '%q'", rootParent, item);
	ret = sql_get_table(db, sql, &result, &rows, &cols);
	sqlite3_free(sql);
	if( cols )
	{
		parentID = strtol(rindex(result[1], '$')+1, NULL, 16);
		objectID = get_next_available_id("OBJECTS", result[1]);
	}
	else
	{
		parentID = get_next_available_id("OBJECTS", rootParent);
		detailID = GetFolderMetadata(item, NULL, artist, genre, album_art, art_dlna_pn);
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, REF_ID, DETAIL_ID, CLASS, NAME) "
					"VALUES"
					" ('%s$%X', '%s', %Q, %lld, 'container.%s', '%q')",
					rootParent, parentID, rootParent, refID, detailID, class, item);
		ret = sql_exec(db, sql);
		sqlite3_free(sql);
	}
	sqlite3_free_table(result);

	return (long long)parentID<<32|objectID;
}

void
insert_containers(const char * name, const char *path, const char * refID, const char * class, long unsigned int detailID)
{
	char sql_buf[128];
	char *sql;
	char **result;
	int ret;
	int cols, row;
	long long int container;

	sprintf(sql_buf, "SELECT * from DETAILS where ID = %lu", detailID);
	ret = sql_get_table(db, sql_buf, &result, &row, &cols);

	if( strstr(class, "imageItem") )
	{
		char *date = result[13+cols], *cam = result[16+cols];
		char date_taken[11];
		static struct virtual_item last_date;
		static struct virtual_item last_cam;
		static struct virtual_item last_camdate;
		static sqlite_int64 last_all_objectID = 0;

		if( date )
		{
			if( *date == '0' )
			{
				strcpy(date_taken, "Unknown");
			}
			else
			{
				strncpy(date_taken, date, 10);
				date_taken[10] = '\0';
			}
			if( strcmp(last_date.name, date_taken) == 0 )
			{
				last_date.objectID++;
				//DEBUG printf("Using last date item: %s/%s/%X\n", last_date.name, last_date.parentID, last_date.objectID);
			}
			else
			{
				container = insert_container(date_taken, "3$12", NULL, "album.photoAlbum", NULL, NULL, NULL, NULL);
				sprintf(last_date.parentID, "3$12$%llX", container>>32);
				last_date.objectID = (int)container;
				strcpy(last_date.name, date_taken);
				//DEBUG printf("Creating cached date item: %s/%s/%X\n", last_date.name, last_date.parentID, last_date.objectID);
			}
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
						"VALUES"
						" ('%s$%X', '%s', '%s', '%s', %lu, %Q)",
						last_date.parentID, last_date.objectID, last_date.parentID, refID, class, detailID, name);
			sql_exec(db, sql);
			sqlite3_free(sql);

			if( cam )
			{
				if( strcmp(cam, last_cam.name) != 0 )
				{
					container = insert_container(cam, "3$13", NULL, "storageFolder", NULL, NULL, NULL, NULL);
					sprintf(last_cam.parentID, "3$13$%llX", container>>32);
					strcpy(last_cam.name, cam);
					last_camdate.name[0] = '\0';
				}
				if( strcmp(last_camdate.name, date_taken) == 0 )
				{
					last_camdate.objectID++;
					//DEBUG printf("Using last camdate item: %s/%s/%s/%X\n", cam, last_camdate.name, last_camdate.parentID, last_camdate.objectID);
				}
				else
				{
					container = insert_container(date_taken, last_cam.parentID, NULL, "album.photoAlbum", NULL, NULL, NULL, NULL);
					sprintf(last_camdate.parentID, "%s$%llX", last_cam.parentID, container>>32);
					last_camdate.objectID = (int)container;
					strcpy(last_camdate.name, date_taken);
					//DEBUG printf("Creating cached camdate item: %s/%s/%s/%X\n", cam, last_camdate.name, last_camdate.parentID, last_camdate.objectID);
				}
				sql = sqlite3_mprintf(	"INSERT into OBJECTS"
							" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
							"VALUES"
							" ('%s$%X', '%s', '%s', '%s', %lu, %Q)",
							last_camdate.parentID, last_camdate.objectID, last_camdate.parentID, refID, class, detailID, name);
				sql_exec(db, sql);
				sqlite3_free(sql);
			}
		}
		/* All Images */
		if( !last_all_objectID )
		{
			last_all_objectID = get_next_available_id("OBJECTS", "3$11");
		}
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
					"VALUES"
					" ('3$11$%llX', '3$11', '%s', '%s', %lu, %Q)",
					last_all_objectID++, refID, class, detailID, name);
		sql_exec(db, sql);
		sqlite3_free(sql);
	}
	else if( strstr(class, "audioItem") )
	{
		char *artist = cols ? result[7+cols]:NULL, *album = cols ? result[8+cols]:NULL, *genre = cols ? result[9+cols]:NULL;
		char *album_art = cols ? result[19+cols]:NULL, *art_dlna_pn = cols ? result[20+cols]:NULL;
		static struct virtual_item last_album;
		static struct virtual_item last_artist;
		static struct virtual_item last_artistalbum;
		static struct virtual_item last_genre;
		static sqlite_int64 last_all_objectID = 0;

		if( album )
		{
			if( strcmp(album, last_album.name) == 0 )
			{
				last_album.objectID++;
				//DEBUG printf("Using last album item: %s/%s/%X\n", last_album.name, last_album.parentID, last_album.objectID);
			}
			else
			{
				strcpy(last_album.name, album);
				container = insert_container(album, "1$7", NULL, "album.musicAlbum", artist, genre, album_art, art_dlna_pn);
				sprintf(last_album.parentID, "1$7$%llX", container>>32);
				last_album.objectID = (int)container;
				//DEBUG printf("Creating cached album item: %s/%s/%X\n", last_album.name, last_album.parentID, last_album.objectID);
			}
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
						"VALUES"
						" ('%s$%X', '%s', '%s', '%s', %lu, %Q)",
						last_album.parentID, last_album.objectID, last_album.parentID, refID, class, detailID, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		if( artist )
		{
			if( strcmp(artist, last_artist.name) != 0 )
			{
				container = insert_container(artist, "1$6", NULL, "person.musicArtist", NULL, genre, NULL, NULL);
				sprintf(last_artist.parentID, "1$6$%llX", container>>32);
				strcpy(last_artist.name, artist);
				last_artistalbum.name[0] = '\0';
			}
			if( strcmp(album?album:"Unknown", last_artistalbum.name) == 0 )
			{
				last_artistalbum.objectID++;
				//DEBUG printf("Using last artist/album item: %s/%s/%X\n", last_artist.name, last_artist.parentID, last_artist.objectID);
			}
			else
			{
				container = insert_container(album?album:"Unknown", last_artist.parentID, album?last_album.parentID:NULL, "album.musicAlbum", artist, genre, album_art, art_dlna_pn);
				sprintf(last_artistalbum.parentID, "%s$%llX", last_artist.parentID, container>>32);
				last_artistalbum.objectID = (int)container;
				strcpy(last_artistalbum.name, album?album:"Unknown");
				//DEBUG printf("Creating cached artist/album item: %s/%s/%X\n", last_artist.name, last_artist.parentID, last_artist.objectID);
			}
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
						"VALUES"
						" ('%s$%X', '%s', '%s', '%s', %lu, %Q)",
						last_artistalbum.parentID, last_artistalbum.objectID, last_artistalbum.parentID, refID, class, detailID, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		if( genre )
		{
			if( strcmp(genre, last_genre.name) == 0 )
			{
				last_genre.objectID++;
				//DEBUG printf("Using last genre item: %s/%s/%X\n", last_genre.name, last_genre.parentID, last_genre.objectID);
			}
			else
			{
				strcpy(last_genre.name, genre);
				container = insert_container(genre, "1$5", NULL, "genre.musicGenre", NULL, NULL, NULL, NULL);
				sprintf(last_genre.parentID, "1$5$%llX", container>>32);
				last_genre.objectID = (int)container;
				//DEBUG printf("Creating cached genre item: %s/%s/%X\n", last_genre.name, last_genre.parentID, last_genre.objectID);
			}
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
						"VALUES"
						" ('%s$%X', '%s', '%s', '%s', %lu, %Q)",
						last_genre.parentID, last_genre.objectID, last_genre.parentID, refID, class, detailID, name);
			sql_exec(db, sql);
			sqlite3_free(sql);
		}
		/* All Music */
		if( !last_all_objectID )
		{
			last_all_objectID = get_next_available_id("OBJECTS", "1$4");
		}
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
					"VALUES"
					" ('1$4$%llX', '1$4', '%s', '%s', %lu, %Q)",
					last_all_objectID++, refID, class, detailID, name);
		sql_exec(db, sql);
		sqlite3_free(sql);
	}
	else if( strstr(class, "videoItem") )
	{
		static sqlite_int64 last_all_objectID = 0;

		/* All Videos */
		if( !last_all_objectID )
		{
			last_all_objectID = get_next_available_id("OBJECTS", "2$8");
		}
		sql = sqlite3_mprintf(	"INSERT into OBJECTS"
					" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
					"VALUES"
					" ('2$8$%llX', '2$8', '%s', '%s', %lu, %Q)",
					last_all_objectID++, refID, class, detailID, name);
		sql_exec(db, sql);
		sqlite3_free(sql);
	}
	sqlite3_free_table(result);
}

int
insert_directory(const char * name, const char * path, const char * base, const char * parentID, int objectID)
{
	char * sql;
	int ret, rows, found = 0;
	sqlite_int64 detailID;
	char * refID = NULL;
	char class[] = "container.storageFolder";
	char * id_buf = NULL;
	char * parent_buf = NULL;
	char **result;
	char *dir = NULL;
	static char last_found[256] = "-1";

	if( strcmp(base, BROWSEDIR_ID) != 0 )
		asprintf(&refID, "%s%s$%X", BROWSEDIR_ID, parentID, objectID);

	if( refID )
	{
 		dir = strdup(path);
		dir = dirname(dir);
		asprintf(&id_buf, "%s%s$%X", base, parentID, objectID);
		asprintf(&parent_buf, "%s%s", base, parentID);
		while( !found )
		{
			if( strcmp(id_buf, last_found) == 0 )
				break;
			sql = sqlite3_mprintf("SELECT count(OBJECT_ID) from OBJECTS where OBJECT_ID = '%s'", id_buf);
			if( (sql_get_table(db, sql, &result, NULL, NULL) == SQLITE_OK) && atoi(result[1]) )
			{
				sqlite3_free_table(result);
				sqlite3_free(sql);
				strcpy(last_found, id_buf);
				break;
			}
			sqlite3_free_table(result);
			sqlite3_free(sql);
			/* Does not exist.  Need to create, and may need to create parents also */
			sql = sqlite3_mprintf("SELECT DETAIL_ID from OBJECTS where OBJECT_ID = '%s'", refID);
			if( (sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK) && rows )
			{
				detailID = atoi(result[1]);
			}
			sqlite3_free_table(result);
			sqlite3_free(sql);
			sql = sqlite3_mprintf(	"INSERT into OBJECTS"
						" (OBJECT_ID, PARENT_ID, REF_ID, DETAIL_ID, CLASS, NAME) "
						"VALUES"
						" ('%s', '%s', %Q, '%lld', '%s', '%q')",
						id_buf, parent_buf, refID, detailID, class, rindex(dir, '/')+1);
			sql_exec(db, sql);
			sqlite3_free(sql);
			if( rindex(id_buf, '$') )
				*rindex(id_buf, '$') = '\0';
			if( rindex(parent_buf, '$') )
				*rindex(parent_buf, '$') = '\0';
			if( rindex(refID, '$') )
				*rindex(refID, '$') = '\0';
			dir = dirname(dir);
		}
		free(refID);
		free(parent_buf);
		free(id_buf);
		free(dir);
		return 1;
	}

	detailID = GetFolderMetadata(name, path, NULL, NULL, NULL, NULL);
	sql = sqlite3_mprintf(	"INSERT into OBJECTS"
				" (OBJECT_ID, PARENT_ID, REF_ID, DETAIL_ID, CLASS, NAME) "
				"VALUES"
				" ('%s%s$%X', '%s%s', %Q, '%lld', '%s', '%q')",
				base, parentID, objectID, base, parentID, refID, detailID, class, name);
	//DEBUG printf("SQL: %s\n", sql);
	ret = sql_exec(db, sql);
	sqlite3_free(sql);
	if( refID )
		free(refID);

	return -1;
}

int
insert_file(char * name, const char * path, const char * parentID, int object)
{
	char *sql;
	char class[32];
	char objectID[64];
	unsigned long int detailID = 0;
	char base[8];
	char * typedir_parentID;
	int typedir_objectID;
	char * baseid;

	static long unsigned int fileno = 0;
	printf("Scanned %lu files...\r", fileno++); fflush(stdout);

	if( is_image(name) )
	{
		strcpy(base, IMAGE_DIR_ID);
		strcpy(class, "item.imageItem.photo");
		detailID = GetImageMetadata(path, name);
	}
	else if( is_audio(name) )
	{
		strcpy(base, MUSIC_DIR_ID);
		strcpy(class, "item.audioItem.musicTrack");
		detailID = GetAudioMetadata(path, name);
	}
	else if( is_video(name) )
	{
		strcpy(base, VIDEO_DIR_ID);
		strcpy(class, "item.videoItem");
		detailID = GetVideoMetadata(path, name);
	}
	//DEBUG printf("Got DetailID %lu!\n", detailID);
	if( !detailID )
		return -1;

	sprintf(objectID, "%s%s$%X", BROWSEDIR_ID, parentID, object);

	sql = sqlite3_mprintf(	"INSERT into OBJECTS"
				" (OBJECT_ID, PARENT_ID, CLASS, DETAIL_ID, NAME) "
				"VALUES"
				" ('%s', '%s%s', '%s', %lu, '%q')",
				objectID, BROWSEDIR_ID, parentID, class, detailID, name);
	//DEBUG printf("SQL: %s\n", sql);
	sql_exec(db, sql);
	sqlite3_free(sql);

	if( *parentID )
	{
		typedir_objectID = 0;
		typedir_parentID = strdup(parentID);
		baseid = rindex(typedir_parentID, '$');
		if( baseid )
		{
			sscanf(baseid+1, "%X", &typedir_objectID);
			*baseid = '\0';
		}
		insert_directory(name, path, base, typedir_parentID, typedir_objectID);
		free(typedir_parentID);
	}
	sql = sqlite3_mprintf(	"INSERT into OBJECTS"
				" (OBJECT_ID, PARENT_ID, REF_ID, CLASS, DETAIL_ID, NAME) "
				"VALUES"
				" ('%s%s$%X', '%s%s', '%s', '%s', %lu, '%q')",
				base, parentID, object, base, parentID, objectID, class, detailID, name);
	//DEBUG printf("SQL: %s\n", sql);
	sql_exec(db, sql);
	sqlite3_free(sql);


	insert_containers(name, path, objectID, class, detailID);
	return 0;
}

int
CreateDatabase(void)
{
	int ret, i;
	char sql_buf[512];
	const char * containers[] = {      "0","-1", "root",
					   "1", "0", "Music",
					 "1$4", "1", "All Music",
					 "1$5", "1", "Genre",
					 "1$6", "1", "Artist",
					 "1$7", "1", "Album",
					"1$14", "1", "Folders",
					   "2", "0", "Video",
					 "2$8", "2", "All Video",
					"3$15", "2", "Folders",
					   "3", "0", "Pictures",
					"3$11", "3", "All Pictures",
					 "3$C", "3", "Date Taken",
					 "3$D", "3", "Camera",
					"3$16", "3", "Folders",
					  "64", "0", "Browse Folders",
					0 };

	sql_exec(db, "pragma temp_store = MEMORY");
	sql_exec(db, "pragma journal_mode = OFF");
	sql_exec(db, "pragma synchronous = OFF;");
	sql_exec(db, "pragma cache_size = 8192;");

	//JM: Set up a db version number, so we know if we need to rebuild due to a new structure.
	sprintf(sql_buf, "pragma user_version = %d;", DB_VERSION);
	sql_exec(db, sql_buf);

	ret = sql_exec(db, "CREATE TABLE OBJECTS ( "
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, "
					"OBJECT_ID TEXT NOT NULL, "
					"PARENT_ID TEXT NOT NULL, "
					"REF_ID TEXT DEFAULT NULL, "
					"CLASS TEXT NOT NULL, "
					"DETAIL_ID INTEGER DEFAULT NULL, "
					"NAME TEXT DEFAULT NULL"
					");");
	if( ret != SQLITE_OK )
		goto sql_failed;
	ret = sql_exec(db, "CREATE TABLE DETAILS ( "
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, "
					"PATH TEXT DEFAULT NULL, "
					"SIZE INTEGER, "
					"TITLE TEXT, "
					"DURATION TEXT, "
					"BITRATE INTEGER, "
					"SAMPLERATE INTEGER, "
					"ARTIST TEXT, "
					"ALBUM TEXT, "
					"GENRE TEXT, "
					"COMMENT TEXT, "
					"CHANNELS INTEGER, "
					"TRACK INTEGER, "
					"DATE DATE, "
					"RESOLUTION TEXT, "
					"THUMBNAIL BOOL DEFAULT 0, "
					"CREATOR TEXT, "
					"DLNA_PN TEXT, "
					"MIME TEXT, "
					"ALBUM_ART INTEGER DEFAULT 0, "
					"ART_DLNA_PN TEXT DEFAULT NULL"
					");");
	if( ret != SQLITE_OK )
		goto sql_failed;
	ret = sql_exec(db, "CREATE TABLE ALBUM_ART ( "
					"ID INTEGER PRIMARY KEY AUTOINCREMENT, "
					"PATH TEXT NOT NULL, "
					"EMBEDDED BOOL DEFAULT 0"
					");");
	if( ret != SQLITE_OK )
		goto sql_failed;
	ret = sql_exec(db, "CREATE TABLE SETTINGS ("
					"UPDATE_ID INTEGER PRIMARY KEY"
					");");
	if( ret != SQLITE_OK )
		goto sql_failed;
	ret = sql_exec(db, "INSERT into SETTINGS values (0)");
	if( ret != SQLITE_OK )
		goto sql_failed;
	for( i=0; containers[i]; i=i+3 )
	{
		sprintf(sql_buf, "INSERT into OBJECTS (OBJECT_ID, PARENT_ID, DETAIL_ID, CLASS, NAME)"
				 " values "
				 "('%s', '%s', %lld, 'container.storageFolder', '%s')",
				 containers[i], containers[i+1], GetFolderMetadata(containers[i+2], NULL, NULL, NULL, NULL, NULL), containers[i+2]);
		ret = sql_exec(db, sql_buf);
		if( ret != SQLITE_OK )
			goto sql_failed;
	}
	sql_exec(db, "create INDEX IDX_OBJECTS_OBJECT_ID ON OBJECTS(OBJECT_ID);");
	sql_exec(db, "create INDEX IDX_OBJECTS_PARENT_ID ON OBJECTS(PARENT_ID);");
	sql_exec(db, "create INDEX IDX_OBJECTS_DETAIL_ID ON OBJECTS(DETAIL_ID);");
	sql_exec(db, "create INDEX IDX_OBJECTS_CLASS ON OBJECTS(CLASS);");
	sql_exec(db, "create INDEX IDX_DETAILS_PATH ON DETAILS(PATH);");
	sql_exec(db, "create INDEX IDX_DETAILS_ID ON DETAILS(ID);");
	sql_exec(db, "create INDEX IDX_ALBUM_ART ON ALBUM_ART(ID);");


sql_failed:
	if( ret != SQLITE_OK )
		fprintf(stderr, "Error creating SQLite3 database!\n");
	return (ret != SQLITE_OK);
}

int
filter_audio(const struct dirent *d)
{
	return ( (*d->d_name != '.') &&
	         ((d->d_type == DT_DIR) ||
		  ((d->d_type == DT_REG) &&
		   is_audio(d->d_name) )
	       ) );
}

int
filter_video(const struct dirent *d)
{
	return ( (*d->d_name != '.') &&
	         ((d->d_type == DT_DIR) ||
		  ((d->d_type == DT_REG) &&
		   is_video(d->d_name) )
	       ) );
}

int
filter_images(const struct dirent *d)
{
	return ( (*d->d_name != '.') &&
	         ((d->d_type == DT_DIR) ||
		  ((d->d_type == DT_REG) &&
		   is_image(d->d_name) )
	       ) );
}

int
filter_media(const struct dirent *d)
{
	return ( (*d->d_name != '.') &&
	         ((d->d_type == DT_DIR) ||
		  ((d->d_type == DT_REG) &&
		   (is_image(d->d_name) ||
		    is_audio(d->d_name) ||
		    is_video(d->d_name)
		   )
	       )) );
}

void
ScanDirectory(const char * dir, const char * parent, enum media_types type)
{
	struct dirent **namelist;
	int n, i, startID = 0;
	char parent_id[PATH_MAX];
	char full_path[PATH_MAX];
	char * name = NULL;

	setlocale(LC_COLLATE, "");
	if( chdir(dir) != 0 )
		return;

	printf("\nScanning %s\n", dir);
	switch( type )
	{
		case ALL_MEDIA:
			n = scandir(".", &namelist, filter_media, alphasort);
			break;
		case AUDIO_ONLY:
			n = scandir(".", &namelist, filter_audio, alphasort);
			break;
		case VIDEO_ONLY:
			n = scandir(".", &namelist, filter_video, alphasort);
			break;
		case IMAGES_ONLY:
			n = scandir(".", &namelist, filter_images, alphasort);
			break;
		default:
			break;
	}
	if (n < 0) {
		fprintf(stderr, "Error scanning %s [scandir]\n", dir);
		return;
	}

	if( !parent )
		startID = get_next_available_id("OBJECTS", BROWSEDIR_ID);

	for (i=0; i < n; i++) {
		sprintf(full_path, "%s/%s", dir, namelist[i]->d_name);
		if( index(namelist[i]->d_name, '&') )
		{
			name = modifyString(strdup(namelist[i]->d_name), "&", "&amp;amp;", 0);
		}
		if( namelist[i]->d_type == DT_DIR )
		{
			insert_directory(name?name:namelist[i]->d_name, full_path, BROWSEDIR_ID, (parent ? parent:""), i+startID);
			sprintf(parent_id, "%s$%X", (parent ? parent:""), i+startID);
			ScanDirectory(full_path, parent_id, type);
		}
		else
		{
			insert_file(name?name:namelist[i]->d_name, full_path, (parent ? parent:""), i+startID);
		}
		if( name )
		{
			free(name);
			name = NULL;
		}
		free(namelist[i]);
	}
	free(namelist);
	if( parent )
	{
		chdir(dirname((char*)dir));
	}
	else
	{
		printf("Scanning %s finished!\n", dir);
	}
}

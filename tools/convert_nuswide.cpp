// This program converts a set of images to a lmdb/leveldb by storing them
// as Datum proto buffers.
// Usage:
//   convert_imageset [FLAGS] ROOTFOLDER/ LISTFILE DB_NAME
//
// where ROOTFOLDER is the root folder that holds all the images, and LISTFILE
// should be a list of files as well as their labels, in the format as
//   subfolder1/file1.JPEG 7
//   ....

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/unordered_set.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <lmdb.h>
#include <sys/stat.h>
#include <algorithm>
#include <fstream>  // NOLINT(readability/streams)
#include <string>
#include <utility>
#include <vector>

#include "caffe/proto/caffe.pb.h"
#include "caffe/util/io.hpp"
#include "caffe/util/rng.hpp"

using namespace caffe;  // NOLINT(build/namespaces)
using std::pair;
using std::string;

DEFINE_bool(gray, false,
    "When this option is on, treat images as grayscale ones");
DEFINE_string(backend, "lmdb", "The backend for storing the result");
DEFINE_int32(resize_width, 256, "Width images are resized to");
DEFINE_int32(resize_height, 256, "Height images are resized to");

DEFINE_int32(text_dim, 100, "dimension of the text vector");
DEFINE_int32(nlabels, 81, "select images with only popular labels");
DEFINE_int32(max_labels, 0, "filter images with more labels than this number");
DEFINE_int32(start, 0, "filter records whose index is before this number");
DEFINE_int32(size, 0, "num of records to insert");
DEFINE_bool(count, false, "just count the valid records number");

// num of images associated with each label, in ascending order
int label_popularity[]={36, 19, 57, 63, 80,  6, 23, 78, 48, 65, 52, 64, 29, 10,
  31, 14, 76, 26, 67, 16, 20, 17, 46,  0, 38, 71, 59, 47, 21, 27,  3, 77, 45, 
  54, 15,  9, 66, 22, 32, 58, 35, 53, 12,  7, 69, 11, 18, 60, 43, 68, 25, 70, 
  28, 37, 61,  4, 73, 33, 40,  5, 39,  2, 72, 56, 74, 51, 49, 62, 24, 50, 41, 
  34, 44, 79,  8, 30,  1, 75, 42, 13, 55};

int main(int argc, char** argv) {
  ::google::InitGoogleLogging(argv[0]);

#ifndef GFLAGS_GFLAGS_H_
  namespace gflags = google;
#endif

  gflags::SetUsageMessage("Convert a set of images to the leveldb/lmdb\n"
        "format used as input for Caffe.\n"
        "Usage:\n"
        "    convert_imageset [FLAGS] ROOTFOLDER/ LISTFILE DB_NAME\n"
        "The ImageNet dataset for the training demo is at\n"
        "    http://www.image-net.org/download-images\n");
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  boost::unordered_set<int> labelset;
  for(int i=81-FLAGS_nlabels;i<81;i++)
    labelset.insert(label_popularity[i]);

  if (argc != 4) {
    gflags::ShowUsageWithFlagsRestrict(argv[0],
                                      "tools/convert_nuswide");
    return 1;
  }
  std::ifstream infile(argv[2]);
  if (!infile.is_open()){
    LOG(FATAL)<<"Cannot open the file "<<argv[2]<<std::endl;
    return 0;
  }

  const string& db_backend = FLAGS_backend;
  const char* db_path = argv[3];

  // Open new db
  // lmdb
  MDB_env *mdb_env;
  MDB_dbi mdb_dbi;
  MDB_val mdb_key, mdb_data;
  MDB_txn *mdb_txn;
  // leveldb
  leveldb::DB* db;
  leveldb::Options options;
  options.error_if_exists = true;
  options.create_if_missing = true;
  options.write_buffer_size = 268435456;
  leveldb::WriteBatch* batch = NULL;

  // Open db
  if (db_backend == "leveldb") {  // leveldb
    LOG(INFO) << "Opening leveldb " << db_path;
    leveldb::Status status = leveldb::DB::Open(
        options, db_path, &db);
    CHECK(status.ok()) << "Failed to open leveldb " << db_path
        << ". Is it already existing?";
    batch = new leveldb::WriteBatch();
  } else if (db_backend == "lmdb") {  // lmdb
    LOG(INFO) << "Opening lmdb " << db_path;
    CHECK_EQ(mkdir(db_path, 0744), 0)
        << "mkdir " << db_path << "failed";
    CHECK_EQ(mdb_env_create(&mdb_env), MDB_SUCCESS) << "mdb_env_create failed";
    CHECK_EQ(mdb_env_set_mapsize(mdb_env, 1099511627776), MDB_SUCCESS)  // 1TB
        << "mdb_env_set_mapsize failed";
    CHECK_EQ(mdb_env_open(mdb_env, db_path, 0, 0664), MDB_SUCCESS)
        << "mdb_env_open failed";
    CHECK_EQ(mdb_txn_begin(mdb_env, NULL, 0, &mdb_txn), MDB_SUCCESS)
        << "mdb_txn_begin failed";
    CHECK_EQ(mdb_open(mdb_txn, NULL, 0, &mdb_dbi), MDB_SUCCESS)
        << "mdb_open failed. Does the lmdb already exist? ";
  } else {
    LOG(FATAL) << "Unknown db backend " << db_backend;
  }
  bool is_color = !FLAGS_gray;
  /*
   * the suffle code is deleted because when splitting train, val, test data,
   * the records are already shuffled.
  std::vector<std::pair<string, int> > lines;
  string filename;
  int label;
  while (infile >> filename >> label) {
    lines.push_back(std::make_pair(filename, label));
  }
  if (FLAGS_shuffle) {
    // randomly shuffle data
    LOG(INFO) << "Shuffling data";
    shuffle(lines.begin(), lines.end());
  }
  LOG(INFO) << "A total of " << lines.size() << " images.";
  */

  int resize_height = std::max<int>(0, FLAGS_resize_height);
  int resize_width = std::max<int>(0, FLAGS_resize_width);

  // Storing to db
  string root_folder(argv[1]);
  int count = 0;
  const int kMaxKeyLength = 256;
  char key_cstr[kMaxKeyLength];
  int data_size;
  bool data_size_initialized = false;
  int line_id=-1;
  while(!infile.eof()){
    Datum datum;
    // parse one line
    string line;
    std::getline(infile, line);
    // must check eof here
    if(infile.eof())
      break;
    std::vector<std::string> strs;
    /* format of line: 
     * <int record_idx> <str image_path> <int label>[ <int label>]#$$#<float vector>
     */
    boost::split(strs, line, boost::is_any_of(" #"));
    string imgpath=strs[1];
    int k=2;
    while(strs[k]!="$$"&&k<strs.size()){
      int labelid=boost::lexical_cast<int>(strs[k]);
      if(labelset.find(labelid)!=labelset.end())
        datum.add_multi_label(labelid);
      k++;
    }
    if((FLAGS_max_labels>0&&datum.multi_label_size()>FLAGS_max_labels)
        ||datum.multi_label_size()==0)
      continue;
    line_id++;
    if(line_id<FLAGS_start||FLAGS_count)
      continue;
    CHECK_LT(k, strs.size());
    k++;
    while(k<strs.size())
      datum.add_text(boost::lexical_cast<float>(strs[k++]));
    CHECK_EQ(datum.text_size(), FLAGS_text_dim)<<"line id "<<line_id;

    // read image, label field is not used, set it to be -1, use multi_label
    if (!ReadImageToDatum(root_folder + "/"+imgpath,
        -1, resize_height, resize_width, is_color, &datum)) {
      continue;
    }
    if (!data_size_initialized) {
      data_size = datum.channels() * datum.height() * datum.width();
      data_size_initialized = true;
    } else {
      const string& data = datum.data();
      CHECK_EQ(data.size(), data_size) << "Incorrect data field size "
          << data.size();
    }
    // sequential
    snprintf(key_cstr, kMaxKeyLength, "%08d_%s", line_id, imgpath.c_str());
    string value;
    datum.SerializeToString(&value);
    string keystr(key_cstr);

    // Put in db
    if (db_backend == "leveldb") {  // leveldb
      batch->Put(keystr, value);
    } else if (db_backend == "lmdb") {  // lmdb
      mdb_data.mv_size = value.size();
      mdb_data.mv_data = reinterpret_cast<void*>(&value[0]);
      mdb_key.mv_size = keystr.size();
      mdb_key.mv_data = reinterpret_cast<void*>(&keystr[0]);
      CHECK_EQ(mdb_put(mdb_txn, mdb_dbi, &mdb_key, &mdb_data, 0), MDB_SUCCESS)
          << "mdb_put failed";
    } else {
      LOG(FATAL) << "Unknown db backend " << db_backend;
    }

    if (++count % 1000 == 0) {
      // Commit txn
      if (db_backend == "leveldb") {  // leveldb
        db->Write(leveldb::WriteOptions(), batch);
        delete batch;
        batch = new leveldb::WriteBatch();
      } else if (db_backend == "lmdb") {  // lmdb
        CHECK_EQ(mdb_txn_commit(mdb_txn), MDB_SUCCESS)
            << "mdb_txn_commit failed";
        CHECK_EQ(mdb_txn_begin(mdb_env, NULL, 0, &mdb_txn), MDB_SUCCESS)
            << "mdb_txn_begin failed";
      } else {
        LOG(FATAL) << "Unknown db backend " << db_backend;
      }
      LOG(ERROR) << "Processed " << count << " files.";
    }
    if(count>=FLAGS_size&&FLAGS_size>0)
      break;
  }
  infile.close();
  // write the last batch
  if (count % 1000 != 0) {
    if (db_backend == "leveldb") {  // leveldb
      db->Write(leveldb::WriteOptions(), batch);
      delete batch;
      delete db;
    } else if (db_backend == "lmdb") {  // lmdb
      CHECK_EQ(mdb_txn_commit(mdb_txn), MDB_SUCCESS) << "mdb_txn_commit failed";
      mdb_close(mdb_env, mdb_dbi);
      LOG(ERROR)<<"after close db!";
      mdb_env_close(mdb_env);
      LOG(ERROR)<<"after close env!";
    } else {
      LOG(FATAL) << "Unknown db backend " << db_backend;
    }
    LOG(ERROR) << "Processed " << count << " files.";
  }
  LOG(ERROR)<<"Finished";
  LOG(ERROR)<<"total lines "<<line_id+1;
  return 0;
}

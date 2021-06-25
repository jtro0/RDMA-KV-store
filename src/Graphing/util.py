import os, fnmatch

def time_in_msec(sec, usec):
    return sec*1000 + usec/1000

def time_in_sec(sec, usec):
    return sec + usec/1000000

def calc_time_difference(start_sec, start_usec, end_sec, end_usec):
    result_sec = end_sec - start_sec
    result_usec = end_usec - start_usec
    if (result_usec < 0):
        --result_sec
        result_usec += 1000000
    return result_sec, result_usec
      
      
def calc_time_difference_sec(start_sec, start_usec, end_sec, end_usec):
    result_sec, result_usec = calc_time_difference(start_sec, start_usec, end_sec, end_usec)
    return time_in_sec(result_sec, result_usec)

def calc_time_difference_msec(start_sec, start_usec, end_sec, end_usec):
    result_sec, result_usec = calc_time_difference(start_sec, start_usec, end_sec, end_usec)
    return time_in_msec(result_sec, result_usec)

def get_all_csv():
    filenames = []
    
    for path, folders, files in os.walk("../../benchmarking/data/non_blocking/"):
        for file in files:
            if fnmatch.fnmatch(file, '*.csv'):
                filenames.append(os.path.join(path, file))
    return filenames

def get_all_csv_blocking():
    filenames = []

    for path, folders, files in os.walk("../../benchmarking/data/blocking/"):
        for file in files:
            if fnmatch.fnmatch(file, '*.csv'):
                filenames.append(os.path.join(path, file))
    return filenames

def get_all_csv_local():
    filenames = []
    
    for path, folders, files in os.walk("../../../First tests/"):
        for file in files:
            if fnmatch.fnmatch(file, '*.csv'):
                filenames.append(os.path.join(path, file))
    return filenames


def get_parts(filename):
    filename_parts = filename.rsplit("/", 1)[-1].rsplit("_", 4)
    
    type = filename_parts[0]
    number_clients = int(filename_parts[1])
    client_number = int(filename_parts[3])
    number_ops = int(filename_parts[4].rsplit(".", 1)[0])
    
    return type, number_clients, client_number, number_ops

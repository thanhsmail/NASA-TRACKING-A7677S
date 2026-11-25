
# -*- coding: utf-8 -*-
import json
import os
import zipfile
import sys
import shutil

def make_new_package_path(package_path):
    print("NEW_Package_path:", NEW_Package_path)
    try:
        os.mkdir(NEW_Package_path)
    except Exception as error:
        del_list = os.listdir(NEW_Package_path)
        for file in del_list:
            os.remove(os.path.join(NEW_Package_path,file)) # dùng để xóa file, os.rmdir() dùng để xóa thư mục
        
    try:
        print("copy package file to new directory")
        shutil.copy(os.path.join(package_path, 'burn.zip'), NEW_Package_path)
        os.rename(os.path.join(NEW_Package_path, 'burn.zip'), os.path.join(NEW_Package_path, 'SC_A7677S_'+module_name+'.zip'))
        # os.remove(os.path.join(package_path,''+module_name+''))

    except Exception as error:
        print(error)
        input('Print any key to abort.')
        sys.exit(1)

def get_dependence(package_path):
    unzip_path = os.path.join(OUT_Package_Path,'dependence')
    try:
        os.mkdir(unzip_path)
    except Exception as error:
        del_list = os.listdir(unzip_path)
        for file in del_list:
            os.remove(os.path.join(unzip_path,file))

    try:
        with zipfile.ZipFile(os.path.join(package_path, 'burn.zip')) as zzz:
            for zzzfile in zzz.namelist():
                if zzzfile not in unneeded_file:
                    zzz.extract(zzzfile,unzip_path)
        # os.remove(os.path.join(package_path,''+module_name+''))

    except Exception as error:
        print(error)
        input('Print any key to abort.')
        sys.exit(1)


def update_json_progress(file_path):
    try:
        with open(os.path.join(file_path,json_name),'r') as f:
            json_data = json.loads(f.read())
    except Exception as error:
        print(error)
        input('print any key to abort.')
        sys.exit(1)
    for json_list_item in json_data[:]:
        #print(json_list_item)
        if json_list_item['group'] != '0' and json_list_item.get('partition','not_exist') != 'customer_app':
            json_data.remove(json_list_item)
    weight = calculate_progress_value(file_path)
    json_data[3]['value'] = weight
    json_data[8]['weight'] = weight
    for json_list_item in json_data:
        print(json_list_item)

    with open(os.path.join(file_path,json_name),'w') as out:
        out.write(json.dumps(json_data))



def calculate_progress_value(file_path):
    try:
        file_size = os.path.getsize(os.path.join(file_path,bin_name))
    except Exception as error:
        print(error)
        input('print any key to abort.')
        sys.exit(1)
    print("Current customer_app patition area is %d KB."%(patition_size/1024))
    return file_size + patition_size


def build_zip(file_path,zip_target_path):
    file_list = os.listdir(file_path)
    #print(file_list)
    zip_obj = zipfile.ZipFile(os.path.join(zip_target_path,zip_name),'w')
    for file in file_list:
        if os.path.isfile(os.path.join(file_path,file)):
            zip_obj.write(os.path.join(file_path,file),arcname = file,compress_type=zipfile.ZIP_DEFLATED)
    
    zip_obj.close()

def remove_dir_path(dir_path):
    try:
        shutil.rmtree(dir_path)   
    except Exception as error:
        print(error)
        input('print any key to abort.')
        sys.exit(1)


print("\n*************************** Start make APP zip ****************************")
unneeded_file = ['bootloader.ubi','ReliableData.bin','cp.bin','dsp.bin','rf.bin','fota_param.bin','system.img']
json_name = 'download.json'
bin_name = 'customer_app.bin'
patition_size = 256*1024

#top_path = os.path.abspath('..')
top_path = os.getcwd()
print("top_path:", top_path)
#ASR_top_path = os.path.abspath(os.path.join(os.getcwd(),'../../../..'))

try:
    module_name = sys.argv[1]
except Exception as error:
    print("no args input!")
    print("use default args:")
    module_name = 'LANV'

print("module_name:", 'A7677S_'+module_name)
zip_name = 'SC_A7677S_'+module_name+'_AppOnly.zip'
print("zip_name:", zip_name)

OUT_Package_Path = os.path.abspath(os.path.join(os.getcwd(),'out'))
print("OUT_Package_Path:", OUT_Package_Path)

output_source = 'A7677S_'+module_name+'_1606_V702_OPENSDK_ENORLFSV2'
OLD_Package_Path = os.path.abspath(os.path.join(OUT_Package_Path, output_source)) # ex: out/A7677S_MANV_1606_V702_OPENSDK
print("OLD_Package_Path:", OLD_Package_Path)

try:
    print("Creat new target folder")
    os.mkdir(os.path.join(top_path, 'target'))
    print("Creat new target folder success!!")
except Exception as error:
    print("Folder target is exist")

NEW_Package_path = os.path.join(top_path, 'target', 'A7677S_'+module_name) # tạo thư mục mới
make_new_package_path(OLD_Package_Path)
get_dependence(OLD_Package_Path)
update_json_progress(os.path.join(OUT_Package_Path,'dependence'))
build_zip(os.path.join(OUT_Package_Path,'dependence'),NEW_Package_path)
remove_dir_path(OUT_Package_Path)


print("******************************* Make zip successful! **********************")
print("******************************* End make APP zip **************************")


input('Print any key to exit...')
# os.system('pause')
sys.exit(0)


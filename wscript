
#
# This file is the default set of rules to compile a Pebble project.
#
# Feel free to customize this to your needs.
#

top = '.'
out = 'build'

def gifs_to_apngs(gifs_dir, appinfo_filename, resources_dir):
    import sys
    import json
    import os.path
    import subprocess
    from os import listdir
    from os.path import isfile, join

    # load old dictionary if possible
    if os.path.lexists(appinfo_filename):
        appinfo_json = json.load(open(appinfo_filename, "rb"))

    #media_entries = appinfo_json['resources']['media']
    media_entries = []

    gifs = [ f for f in listdir(gifs_dir) if isfile(join(gifs_dir,f)) ]
    gifs.sort()
    media_entries.append({
        "characterRegex": "[ :0-9]", 
        "type": "font", 
        "name": "FONT_BOXY_TEXT_30", 
        "file": "Boxy_Text.ttf"
      })
    media_entries.append({
        "characterRegex": "[ :0-9]", 
        "type": "font", 
        "name": "FONT_BOXY_OUTLINE_30", 
        "file": "Boxy_Outline.ttf"
      })
    media_entries.append({
        "characterRegex": "[ /0-9MTWFSadehinortu]", 
        "type": "font", 
        "name": "FONT_BOXY_TEXT_18", 
        "file": "Boxy_Text.ttf"
      })
    media_entries.append({
        "characterRegex": "[ /0-9MTWFSadehinortu]", 
        "type": "font", 
        "name": "FONT_BOXY_OUTLINE_18", 
        "file": "Boxy_Outline.ttf"
      })

    #use gifsicle and gif2apng to convert gifs to pebble compatible apngs
    #and add to appinfo.json file under resources/media
    i = 1
    for gif in gifs:
        if '.gif' in gif:
            png = gif.replace('.gif', '.png')
            gif_mod = gif.replace('.gif', '.mod.gif')
            subprocess.call('gifsicle --resize-fit 144x144 ' + 
            '--resize-method lanczos3 ' + '--colors 64 -O1 -o ' + resources_dir + '/' + 
            gif_mod + ' ' + gifs_dir + '/' + gif, shell=True)
            subprocess.call('gif2apng_noprev -z0 ' + resources_dir + '/' + gif_mod + ' ' + 
            resources_dir + '/' + png, shell=True)
            media_entries.append({"type":"raw","name":"IMAGE_" + str(i), "file":png})
            i = i + 1

    appinfo_json['resources']['media'] = media_entries

    # write the json dictionary
    json.dump(appinfo_json, open(appinfo_filename, "wb"), indent=2, sort_keys=False)
    return True

def options(ctx):
    ctx.load('pebble_sdk')

def configure(ctx):
    gifs_to_apngs('gifs', 'appinfo.json', 'resources')
    ctx.load('pebble_sdk')

def build(ctx):
    import os.path
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    for p in ctx.env.TARGET_PLATFORMS:
        ctx.set_env(ctx.all_envs[p])
        app_elf='{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_program(source=ctx.path.ant_glob('src/**/*.c'),
        target=app_elf)

        if build_worker:
            worker_elf='{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': p, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_worker(source=ctx.path.ant_glob('worker_src/**/*.c'),
            target=worker_elf)
        else:
            binaries.append({'platform': p, 'app_elf': app_elf})

    ctx.pbl_bundle(binaries=binaries, js=ctx.path.ant_glob('src/js/**/*.js'))

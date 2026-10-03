#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2023 Jean-Baptiste Mardelle <jb@kdenlive.org>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import os
import sys
import requests
import whisper


def main(**kwargs):
    kwargs_def = {
        'task': '',
        'model': '',
        'url': '',
        'download_root': ''
    }
    assert all(k in kwargs_def for k in kwargs), f"Invalid kwargs: {kwargs.keys()}"
    kwargs = {**kwargs_def, **kwargs}
    task = kwargs['task']
    if task == "list":
        models = whisper.available_models()
        for m in models:
            url = whisper._MODELS[m]
            print(m + " : " + url, flush=True)
        default = os.path.join(os.path.expanduser("~"), ".cache")
        root = os.path.join(os.getenv("XDG_CACHE_HOME", default), "whisper")
        print("root_folder : " + root, flush=True)
    elif task == "size":
        model = kwargs['model']
        if model == '':
            print('Please give a model name', flush=True)
            # Abort
            sys.exit()

        url = whisper._MODELS[model]
        if url == '':
            print('Cannot find url for model name', flush=True)
            # Abort
            sys.exit()
        default = os.path.join(os.path.expanduser("~"), ".cache")
        root = os.path.join(os.getenv("XDG_CACHE_HOME", default), "whisper")
        download_target = os.path.join(root, os.path.basename(url))
        if os.path.exists(download_target) and os.path.isfile(download_target):
            # model is already downloaded
            print(model + " : 0", flush=True)
        else:
            resp = requests.get(url, stream=True)
            if resp.status_code != 200:
                # not ok, not found or maybe offline
                print(model + " : -1", flush=True)
            else:
                size = resp.headers.get("Content-length")
                print(model + " : " + str(size), flush=True)
    elif task == "download":
        url = kwargs['url']
        path = kwargs['download_root']
        if url == '' or path == '':
            print('Please give an url and a path', flush=True)
            sys.exit(1)
        os.makedirs(path, exist_ok=True)
        target_file = os.path.join(path, os.path.basename(url))
        try:
            resp = requests.get(url, stream=True, timeout=30)
            resp.raise_for_status()
            total_size = int(resp.headers.get('content-length', 0))
            block_size = 1024 * 1024  # 1 MB chunk
            downloaded = 0
            last_percent = -1

            with open(target_file + ".tmp", 'wb') as f:
                for chunk in resp.iter_content(chunk_size=block_size):
                    if chunk:
                        f.write(chunk)
                        downloaded += len(chunk)
                        if total_size > 0:
                            percent = int((downloaded / total_size) * 100)
                            if percent != last_percent:
                                print(f"{percent} %", flush=True)
                                last_percent = percent
            
            if os.path.exists(target_file):
                os.remove(target_file)
            os.rename(target_file + ".tmp", target_file)
            print("100 %", flush=True)
        except Exception as e:
            if os.path.exists(target_file + ".tmp"):
                os.remove(target_file + ".tmp")
            print(f"Error downloading model: {e}", file=sys.stderr, flush=True)
            # Fallback to internal download
            whisper._download(url, path, False)
    else:
        print("Usage:", flush=True)
        print("task=list : list available models", flush=True)

    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main(**dict(arg.split('=') for arg in sys.argv[1:])))

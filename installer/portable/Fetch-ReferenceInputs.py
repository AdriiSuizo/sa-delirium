"""Build-time fetch of ORIGINAL author distributions (not a runtime dependency).

Uses ordinary HTTPS only. Authentication/challenges/errors remain errors. The
result pins hashes; a changed upstream package must undergo a new content audit.
"""
from pathlib import Path
import argparse
import hashlib
import json
import requests
import html
import re
import time
from html.parser import HTMLParser
from urllib.parse import urlsplit, urljoin

class PublicForm(HTMLParser):
    def __init__(self):
        super().__init__();self.inside=False;self.fields={};self.action=''
    def handle_starttag(self,tag,attributes):
        a=dict(attributes)
        if tag=='form' and a.get('name')=='F1':self.inside=True;self.action=a.get('action','')
        if self.inside and tag=='input' and a.get('name'):self.fields[a['name']]=a.get('value','')
    def handle_endtag(self,tag):
        if tag=='form':self.inside=False

def original_url(session,item):
    if item.get('resolver')!='sharemods_form':return item['url']
    response=session.get(item['url'],timeout=30);response.raise_for_status()
    if any(x in response.text for x in ['g-recaptcha','h-captcha','cf-chl-']):raise ValueError('human_validation_required')
    form=PublicForm();form.feed(response.text)
    if form.fields.get('op')!='download2' or not form.fields.get('id'):raise ValueError('Provider public form changed')
    # Ordinary free-download form, retaining its server-issued fields and wait.
    time.sleep(10)
    action=urljoin(item['url'],form.action)
    if urlsplit(action).hostname!='sharemods.com':raise ValueError('Unexpected download form host')
    response=session.post(action,data=form.fields,timeout=30);response.raise_for_status()
    for url in re.findall(r'href=["\x27](https://[^"\x27]+)',response.text):
        url=html.unescape(url);host=(urlsplit(url).hostname or '').lower()
        if host=='sharem.s3.de.io.cloud.ovh.net' or host.endswith('.r2.cloudflarestorage.com'):return url
    raise ValueError('Official download link unavailable; use normal provider page')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--spec', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    receipt = args.out / 'fetch-receipt.json'
    records = json.loads(receipt.read_text(encoding='utf-8')) if receipt.exists() else {}
    for item in json.loads(args.spec.read_text(encoding='utf-8')):
        key = item['id']
        path = args.out / item['filename']
        try:
            if not path.exists():
                session=requests.Session()
                url=original_url(session,item)
                with session.get(url, stream=True, timeout=(20, 120)) as response:
                    response.raise_for_status()
                    if 'text/html' in response.headers.get('Content-Type', ''):
                        raise ValueError('Provider returned a page; use normal browser download, not an archive')
                    with path.with_suffix(path.suffix + '.partial').open('wb') as output:
                        for block in response.iter_content(1024 * 1024): output.write(block)
                path.with_suffix(path.suffix + '.partial').rename(path)
            digest = hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()
            if item.get('sha256') and digest != item['sha256']:
                raise ValueError('Original package hash mismatch')
            records[key] = dict(item, sha256=digest, bytes=path.stat().st_size, status='fetched')
            print(key, 'fetched', path.stat().st_size, flush=True)
        except Exception as error:
            records[key] = dict(item, status='blocked', error=str(error))
            print(key, 'blocked', str(error)[:180], flush=True)
        receipt.write_text(json.dumps(records, indent=2, ensure_ascii=False), encoding='utf-8')

if __name__ == '__main__': main()

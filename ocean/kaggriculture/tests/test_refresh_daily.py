import io
from pathlib import Path
import sys
from unittest.mock import patch
import zipfile
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import refresh_daily_replays as refresh
import refresh_reset_archives as bounded


def archive_bytes():
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w') as archive:
        archive.writestr('episode.json', '{}')
    return stream.getvalue()


@pytest.mark.parametrize('resume', [False, True])
def test_download_and_existing_publication(tmp_path, resume):
    data = archive_bytes()
    path = tmp_path / 'archive.zip'
    offset = 12 if resume else 0
    if resume:
        (tmp_path / '.archive.zip.part').write_bytes(data[:offset])
    response = io.BytesIO(data[offset:])
    response.status = 206 if resume else 200
    response.headers = {'Content-Range': f'bytes {offset}-{len(data)-1}/{len(data)}'}
    with patch.object(refresh.urllib.request, 'urlopen', return_value=response) as request:
        size, resumed = refresh._download_resumable('https://example.test/archive', path)
    assert path.read_bytes() == data and size == len(data) and resumed == resume
    assert request.call_args.args[0].get_header('Range') == (f'bytes={offset}-' if resume else None)
    response = io.BytesIO(data)
    response.status, response.headers = 200, {}
    with patch.object(refresh.urllib.request, 'urlopen', return_value=response):
        with pytest.raises(FileExistsError):
            refresh._download_resumable('https://example.test/archive', path)
    assert path.read_bytes() == data


def test_invalid_download_not_published(tmp_path):
    response = io.BytesIO(b'not a zip')
    response.status, response.headers = 200, {}
    with patch.object(refresh.urllib.request, 'urlopen', return_value=response):
        with pytest.raises(ValueError, match='complete ZIP'):
            refresh._download_resumable('https://example.test/archive', tmp_path / 'bad.zip')
    assert not (tmp_path / 'bad.zip').exists()


def test_index_and_date_filter():
    raw = (b'date,daily_dataset_slug,daily_dataset_url,episode_count,total_bytes\n'
           b'2026-09-20,kaggriculture-episodes-2026-09-20,url,1,200\n'
           b'2026-09-21,kaggriculture-episodes-2026-09-21,url,2,400\n')
    with patch.object(refresh, '_fetch', return_value=raw):
        rows = refresh.read_index()
    assert rows[0]['date'] == '2026-09-21'
    selected = refresh._select_rows(rows, since=None, until=refresh._date('2026-09-20'),
                                    days=1, slugs=set())
    assert len(selected) == 1 and selected[0]['date'] == '2026-09-20'


@pytest.mark.parametrize('budget,free,error', [(0, 2**40, 'budget'), (1, 0, 'reserve')])
def test_bounded_refresh_checks_before_download(tmp_path, budget, free, error):
    row = dict(date='2026-09-20', daily_dataset_slug='kaggriculture-episodes-2026-09-20')
    response = io.BytesIO(b'x')
    response.headers = {'Content-Range': 'bytes 0-0/1000'}
    argv = ['refresh', '--root', str(tmp_path), '--since', '2026-09-20',
            '--until', '2026-09-20', '--report', str(tmp_path / 'report.json'),
            '--reserve-gib', '0', '--max-download-gib', str(budget)]
    with patch.object(sys, 'argv', argv), patch.object(refresh, 'read_index', return_value=[row]), \
            patch.object(bounded.urllib.request, 'urlopen', return_value=response), \
            patch.object(bounded.shutil, 'disk_usage', return_value=SimpleNamespace(free=free)), \
            patch.object(refresh, 'refresh') as download:
        with pytest.raises(RuntimeError, match=error):
            bounded.main()
        download.assert_not_called()


def test_wrong_range_not_appended(tmp_path):
    partial = tmp_path / '.archive.zip.part'
    partial.write_bytes(b'prefix')
    response = io.BytesIO(b'wrong offset')
    response.status, response.headers = 206, {'Content-Range': 'bytes 0-11/12'}
    with patch.object(refresh.urllib.request, 'urlopen', return_value=response):
        with pytest.raises(ValueError, match='wrong offset'):
            refresh._download_resumable('https://example.test/archive', tmp_path / 'archive.zip')
    assert partial.read_bytes() == b'prefix'

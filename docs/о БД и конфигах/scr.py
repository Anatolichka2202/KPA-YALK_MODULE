import pandas as pd
import re
import sqlite3
from pathlib import Path
import warnings
warnings.filterwarnings('ignore')

# ------------------------------------------------------------
# 1. Парсинг текстовых адресов
# ------------------------------------------------------------
def parse_address_line(line: str):
    line = line.strip().replace('Ď', 'П').replace('Ě', 'M')
    pattern = re.compile(
        r'^[MМ](\d+)[ПP]([12])'
        r'(?:[Aa](\d+))?'
        r'(?:[Bb](\d+))?'
        r'(?:[Cc](\d+))?'
        r'(?:[Dd](\d+))?'
        r'(?:[Ee](\d+))?'
        r'(?:[Xx](\d+))?'
        r'(?:[Tt](\d+))?'
        r'(?:[Pp](\d+))?'
        r'$'
    )
    match = pattern.match(line)
    if not match:
        return None
    g = match.groups()
    return {
        'M': g[0],
        'Поток': g[1],
        'A': g[2] or '',
        'B': g[3] or '',
        'C': g[4] or '',
        'D': g[5] or '',
        'E': g[6] or '',
        'X': g[7] or '',
        'T': g[8] or '',
        'P': g[9] or '',
        'full': line
    }

def load_addresses_from_files(directory: str):
    addresses = []
    path = Path(directory)
    for file_path in path.glob('*.txt'):
        for encoding in ['cp1251', 'utf-8']:
            try:
                with open(file_path, 'r', encoding=encoding) as f:
                    lines = f.readlines()
                break
            except UnicodeDecodeError:
                continue
        else:
            print(f"Не удалось прочитать {file_path.name}")
            continue
        for line in lines:
            line = line.strip()
            if not line:
                continue
            addr = parse_address_line(line)
            if addr:
                addresses.append(addr)
    return addresses

# ------------------------------------------------------------
# 2. Загрузка Excel (параметры)
# ------------------------------------------------------------
def load_excel_parameters(excel_path: str):
    df_raw = pd.read_excel(excel_path, sheet_name=0, header=None, dtype=str).fillna('')
    main_idx = {
        '№': 0, 'Параметр': 1, 'Индекс': 2, 'Поток': 3,
        'А': 4, 'В': 5, 'С': 6, 'D': 7, 'E': 8, 'X': 9, 'T': 10, 'P': 11
    }
    # Поиск колонки потока ЗУ
    zu_stream_col = None
    for col in range(12, len(df_raw.columns)):
        for _, row in df_raw.iterrows():
            val = str(row[col]).strip()
            if val in ('П1', 'П2'):
                zu_stream_col = col
                break
        if zu_stream_col is not None:
            break
    zu_idx = {}
    if zu_stream_col is not None:
        zu_idx['Поток_зу'] = zu_stream_col
        for i, letter in enumerate(['А_зу', 'В_зу', 'С_зу', 'D_зу', 'E_зу', 'X_зу', 'T_зу', 'P_зу']):
            zu_idx[letter] = zu_stream_col + 1 + i
    records = []
    for _, row in df_raw.iterrows():
        rec = {}
        for name, col in main_idx.items():
            rec[name] = str(row[col]).strip() if col < len(row) else ''
        for name, col in zu_idx.items():
            rec[name] = str(row[col]).strip() if col < len(row) else ''
        if not rec['№'] and not rec['Параметр']:
            continue
        records.append(rec)
    df = pd.DataFrame(records)
    if 'Индекс' in df.columns:
        df.drop(columns=['Индекс'], inplace=True)
    return df

# ------------------------------------------------------------
# 3. Сопоставление адреса
# ------------------------------------------------------------
def match_address(row, addresses, is_zu=False):
    if is_zu:
        stream_raw = row.get('Поток_зу', '').strip()
        if not stream_raw:
            return None, None
        stream = stream_raw.replace('П', '')
        fields = ['А_зу','В_зу','С_зу','D_зу','E_зу','X_зу','T_зу','P_зу']
        target = ['A','B','C','D','E','X','T','P']
        t_field = 'T_зу'
    else:
        stream_raw = row.get('Поток', '').strip()
        if not stream_raw:
            return None, None
        stream = stream_raw.replace('П', '')
        fields = ['А','В','С','D','E','X','T','P']
        target = ['A','B','C','D','E','X','T','P']
        t_field = 'T'
    param = {}
    t_val = row.get(t_field, '').strip()
    for f, t in zip(fields, target):
        val = row.get(f, '').strip()
        if val and val != 'nan' and val != '':
            if t == 'P' and t_val != '05':
                continue
            param[t] = val
    candidates = []
    for addr in addresses:
        if addr['Поток'] != stream:
            continue
        match = True
        for t, val in param.items():
            if addr.get(t, '') != val:
                match = False
                break
        if match:
            candidates.append(addr)
    if not candidates:
        return None, None
    best = candidates[0]
    return best['M'], best['full']

# ------------------------------------------------------------
# 4. Категория по номеру
# ------------------------------------------------------------
def get_category(num_str):
    if not num_str or not num_str.isdigit():
        return 'Служебные'
    n = int(num_str)
    if 1 <= n <= 16:
        return 'Высокочастотные и быстроменяющиеся'
    elif 18 <= n <= 81:
        return 'БСИ МКА-1 аналоговые'
    elif 82 <= n <= 148:
        return 'БСИ МКС-1 контактные'
    elif 149 <= n <= 152:
        return 'УБСИ МКА-2'
    elif 153 <= n <= 168:
        return 'БСИ МКА-2 аналоговые'
    elif 169 <= n <= 184:
        return 'БСИ МКА-3 аналоговые'
    elif 185 <= n <= 216:
        return 'УБСИ МКА-4'
    elif 217 <= n <= 227:
        return 'УБСИ МКС-2 аналоговые'
    elif 228 <= n <= 241:
        return 'УБСИ МКС-3 контактные'
    elif 242 <= n <= 251:
        return 'УБСИ МКА-7'
    elif 252 <= n <= 255:
        return 'УБСИ МКА-8'
    elif 256 <= n <= 319:
        return 'БСИ температурные ЯТП-1'
    elif 320 <= n <= 383:
        return 'УБСИ температурные ЯТП-2'
    else:
        return 'Другое'

# ------------------------------------------------------------
# 5. Постобработка: проставить информативность по категории
# ------------------------------------------------------------
def fill_informativnost_by_category(df):
    for cat in df['Категория'].unique():
        sample = df[(df['Категория'] == cat) & (df['Информативность'] != '')]['Информативность']
        if not sample.empty:
            info_val = sample.iloc[0]
            mask = (df['Категория'] == cat) & (df['Информативность'] == '')
            df.loc[mask, 'Информативность'] = info_val
    return df

# ------------------------------------------------------------
# 6. Приведение к финальному порядку колонок (ДОБАВЛЕН ПАРАМЕТР)
# ------------------------------------------------------------
def finalize_columns(df):
    base_cols = [
        'Информативность', 'Параметр',      # <--- добавлено
        'Поток', 'А', 'В', 'С', 'D', 'E', 'X', 'T', 'P',
        'Полный адрес', 'Категория'
    ]
    zu_cols = []
    if 'Поток_зу' in df.columns:
        zu_cols = [
            'Поток_зу',
            'А_зу', 'В_зу', 'С_зу', 'D_зу', 'E_зу', 'X_зу', 'T_зу', 'P_зу',
            'Полный адрес_зу'
        ]
    if 'Информативность_зу' in df.columns:
        zu_cols.insert(0, 'Информативность_зу')
    final_cols = base_cols + zu_cols
    existing = [c for c in final_cols if c in df.columns]
    return df[existing]

# ------------------------------------------------------------
# 7. Сохранение в SQLite
# ------------------------------------------------------------
def save_to_sqlite(df, db_path='parameters.db'):
    conn = sqlite3.connect(db_path)
    df.to_sql('parameters', conn, if_exists='replace', index=False)
    conn.close()
    print(f"База данных сохранена в {db_path}")

# ------------------------------------------------------------
# 8. Основная сборка
# ------------------------------------------------------------
def main():
    excel_file = 'Лист Microsoft Excel.xlsx'
    addresses_dir = './'
    output_excel = 'База_данных_финальная.xlsx'
    db_file = 'parameters.db'

    print("Загрузка параметров из Excel...")
    df = load_excel_parameters(excel_file)
    print(f"Загружено {len(df)} строк, колонки: {list(df.columns)}")

    print("Загрузка адресов из текстовых файлов...")
    addresses = load_addresses_from_files(addresses_dir)
    print(f"Загружено {len(addresses)} адресных строк")

    # Добавляем колонки для результатов
    df['Информативность'] = ''
    df['Полный адрес'] = ''
    df['Информативность_зу'] = ''
    df['Полный адрес_зу'] = ''

    # Сопоставление
    for idx, row in df.iterrows():
        m, full = match_address(row, addresses, is_zu=False)
        if m:
            df.at[idx, 'Информативность'] = m
            df.at[idx, 'Полный адрес'] = full
        m_zu, full_zu = match_address(row, addresses, is_zu=True)
        if m_zu:
            df.at[idx, 'Информативность_зу'] = m_zu
            df.at[idx, 'Полный адрес_зу'] = full_zu

    # Категория
    df['Категория'] = df['№'].apply(get_category)

    # Постобработка: проставляем информативность по категории
    df = fill_informativnost_by_category(df)

    # Финальные колонки (теперь с 'Параметр')
    df_final = finalize_columns(df)

    # Сохраняем Excel
    df_final.to_excel(output_excel, index=False)
    print(f"Финальный Excel сохранён в {output_excel}")

    # Сохраняем SQLite
    save_to_sqlite(df_final, db_file)

    # Статистика
    matched = (df_final['Информативность'] != '').sum()
    print(f"После постобработки информативность есть у {matched}/{len(df_final)} записей")
    if 'Информативность_зу' in df_final.columns:
        matched_zu = (df_final['Информативность_зу'] != '').sum()
        print(f"Информативность ЗУ есть у {matched_zu}/{len(df_final)} записей")

if __name__ == '__main__':
    main()
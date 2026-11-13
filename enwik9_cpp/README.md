# enwik9 Datasheet Generator (C++17)

Chương trình tạo hai nhóm thống kê:

- **Data 1**: tần suất các chuỗi byte liên tiếp độ dài 1..N. Bỏ qua `TAB`, `LF`, `CR`, `SPACE`; các ký tự này đồng thời ngắt cửa sổ chuỗi, vì vậy không tạo n-gram bắc qua khoảng trắng.
- **Data 3**: chia file thành các chunk kích thước khác nhau; trong mỗi chunk đếm histogram 256 giá trị byte và thống kê XML element tag hoàn chỉnh. Tag bị cắt qua biên chunk được giữ ở `carry` và chỉ đếm khi dấu `>` hoàn tất xuất hiện ở chunk sau.

## Build

Linux (Ubuntu):

```bash
sudo apt update
sudo apt install build-essential cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Không cần thư viện ngoài.

## Run

```bash
# Chạy cả Data 1 và Data 3
./build/enwik9_datasheet /path/to/enwik9 all 5 500000

# Chỉ Data 1: n-gram tối đa 5 byte, spill khi có 500k key khác nhau
./build/enwik9_datasheet /path/to/enwik9 data1 5 500000

# Chỉ Data 3: chunk size tính bằng byte thập phân
./build/enwik9_datasheet /path/to/enwik9 data3 1000000,2000000,5000000,10000000
```

`1 MB = 1,000,000 bytes`; `1 MiB = 1,048,576 bytes`. Ví dụ trên dùng MB thập phân.

## Output

### Data 1

- `output/data1.bin`: file nhị phân có magic `D1NG`, version 1; record được sắp xếp từ điển:
  - `key_length`: 1 byte
  - `key`: raw bytes
  - `count`: unsigned varint
- `output/data1.meta.txt`: metadata, tổng số key khác nhau, cấu hình.

Data 1 dùng hash map theo lô. Khi số key khác nhau đạt `max_unique_entries`, dữ liệu được sort và spill ra run tạm; cuối cùng merge các run để tạo file kết quả. Điều này giảm rủi ro dùng RAM tăng không kiểm soát, nhưng ổ đĩa cần đủ chỗ trống cho run tạm và file output.

### Data 3

Mỗi chunk trong `.bin` gồm:
- chunk index, offset, số byte thực tế (varint)
- histogram byte dạng sparse: số giá trị khác 0, rồi từng cặp `(byte_id: u8, count: varint)`
- histogram tag: số tag, rồi `(tag_name: length-prefixed string, count: varint)`
- reserved extension string
- độ dài carry-in và nội dung carry-out (carry-out của chunk trước được giữ lại để ghép khi xử lý chunk sau)

File `.meta.txt` và `_summary.csv` cung cấp số chunk, số byte, số byte-value có xuất hiện, số loại tag và carry. `output/data3_comparison.csv` so sánh kích thước datasheet thực tế giữa các chunk size.

Tag được chuẩn hóa theo tên phần tử: `page`, `/page`; thuộc tính bị bỏ qua. Tag mở và đóng được đếm riêng. Comment, CDATA, processing instruction và declaration không được tính là element tag. Nếu token chưa hoàn chỉnh ở cuối file, nó được ghi nhận trong metadata nhưng không được tính là tag.

## Lưu ý về diễn giải dữ liệu

Các thống kê là đặc trưng/phân bố và ràng buộc hỗ trợ dự đoán/phục hồi, không giữ lại thứ tự byte gốc. Chúng không đủ để tái tạo chính xác file bị mất nếu không có dữ liệu hoặc thông tin bổ sung.

## Giới hạn / điểm cần kiểm tra

- Cần chạy thử trên file mẫu nhỏ và đối chiếu với XML parser chuẩn trước khi dùng kết quả làm dữ liệu nghiên cứu.
- Data 3 hiện đọc từng chunk vào RAM; kích thước chunk lớn nhất chính là mức RAM cần thiết xấp xỉ cho dữ liệu chunk cộng buffer/carry.
- Data 1 có thể sinh rất nhiều key; giảm `max_unique_entries` để giảm RAM, đổi lại nhiều run tạm và thời gian merge.
- Bộ phân tích XML xử lý boundary, dấu nháy trong attribute, comment, CDATA, PI và declaration cơ bản; đây không phải XML validator đầy đủ.

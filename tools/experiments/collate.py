import csv

with open("marking-1-teams.csv", "r", newline="", encoding="utf-8") as source:
    first_line = source.readline()

with open("marking-teams-results.csv", "w", newline="", encoding="utf-8") as results:
    results.write(first_line)

    for i in range(1, 21):
        filename = f"marking-{i}-teams.csv"

        try:
            with open(filename, "r", newline="", encoding="utf-8") as source:
                reader = csv.reader(source)

                for row in reader:
                    if "Aston Villa" in row:
                        writer = csv.writer(results)
                        row[1] = f"{i}"
                        writer.writerow(row)
                        break
        except FileNotFoundError:
            continue